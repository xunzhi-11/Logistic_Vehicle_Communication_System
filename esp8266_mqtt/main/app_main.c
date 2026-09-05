#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "driver/uart.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#define UART_NUM        UART_NUM_0
#define RX_BUF_SIZE     512
#define RX_TASK_STACK   3072
#define UART_EVT_QUEUE_LEN 20

// Buffer/message pool used for UART -> MQTT flow
#define BUF_COUNT 40
#define BUF_SIZE 90

#define RING_BUFFER_SIZE      2048    // 2KB环形缓冲，可存约20个100字节包
#define RING_BUF_THRESHOLD    2048    // 75%阈值，超过时触发流控
#define SERIAL_TX_QUEUE_SIZE  15      // 发送队列深度

// 环形缓冲区结构
typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    size_t head;      // 写指针（生产者）
    size_t tail;      // 读指针（消费者）
    size_t count;     // 当前数据量
    SemaphoreHandle_t mutex;  // 保护环形缓冲
} ring_buffer_t;

// 发送队列项
typedef struct {
    uint8_t data[256];  // 固定大小，避免动态内存
    size_t len;
} tx_queue_item_t;

typedef struct {
    uint8_t data[BUF_SIZE];
    size_t len;
} buf_t;

static buf_t buf_pool[BUF_COUNT];
static QueueHandle_t free_buf_queue = NULL;
static uint32_t buf_alloc_fail_count = 0;
static uint32_t buf_queue_drop_count = 0;


static SemaphoreHandle_t s_serial_mux = NULL;
static const char *TAG = "MQTT_EXAMPLE";
static QueueHandle_t uart_queue = NULL;
static void (*rx_cb)(const uint8_t*, size_t, void*) = NULL;
static void *rx_cb_ctx = NULL;
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static volatile bool g_mqtt_connected = false;
static QueueHandle_t publish_queue = NULL;
static ring_buffer_t g_ring_buffer;
static TaskHandle_t g_serial_tx_task = NULL;
static QueueHandle_t g_serial_tx_queue = NULL;
static bool g_flow_control_active = false;

// 统计信息
static uint32_t g_mqtt_received = 0;
static uint32_t g_serial_sent = 0;
static uint32_t g_ringbuf_dropped = 0;
static uint32_t g_queue_dropped = 0;
static uint32_t g_flow_control_events = 0;


static void bufpool_init(void)
{
    if (free_buf_queue == NULL) {
        free_buf_queue = xQueueCreate(BUF_COUNT, sizeof(buf_t *));
        for (int i = 0; i < BUF_COUNT; i++) {
            buf_t *b = &buf_pool[i];
            b->len = 0;
            xQueueSend(free_buf_queue, &b, 0);
        }
    }
}

static buf_t *buf_alloc(TickType_t ticks_to_wait)
{
    buf_t *b = NULL;
    if (xQueueReceive(free_buf_queue, &b, ticks_to_wait) == pdTRUE) {
        return b;
    } else {
        buf_alloc_fail_count++;
        return NULL;
    }
}

static void buf_free(buf_t *b)
{
    if (b) {
        b->len = 0;
        xQueueSend(free_buf_queue, &b, 0);
    }
}

static void ring_buffer_init(ring_buffer_t *rb) {
    memset(rb->buffer, 0, RING_BUFFER_SIZE);
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->mutex = xSemaphoreCreateMutex();
    if (rb->mutex == NULL) {
       // ESP_LOGE(TAG, "创建环形缓冲互斥量失败");
    }
}

// 获取可用空间（线程安全）
static size_t ring_buffer_free_space(ring_buffer_t *rb) {
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    size_t free_space = RING_BUFFER_SIZE - rb->count;
    xSemaphoreGive(rb->mutex);
    return free_space;
}

// 写入数据到环形缓冲（线程安全）
static bool ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t len) {
    if (len == 0 || len > RING_BUFFER_SIZE) {
        return false;
    }
    
    if (xSemaphoreTake(rb->mutex, 0) != pdTRUE) {
        //ESP_LOGW(TAG, "获取环形缓冲锁失败");
        return false;
    }
    
    // 检查空间是否足够
    if (len > (RING_BUFFER_SIZE - rb->count)) {
        xSemaphoreGive(rb->mutex);
        g_ringbuf_dropped++;
        return false;
    }
    
    // 写入数据
    size_t write_pos = rb->head;
    for (size_t i = 0; i < len; i++) {
        rb->buffer[write_pos] = data[i];
        write_pos = (write_pos + 1) % RING_BUFFER_SIZE;
    }
    
    // 更新指针和计数
    rb->head = write_pos;
    rb->count += len;
    
    // 检查是否需要流控
    bool need_flow_control = (rb->count >= RING_BUF_THRESHOLD);
    
    xSemaphoreGive(rb->mutex);
    
    // 触发流控（在锁外执行，避免死锁）
    if (need_flow_control && !g_flow_control_active) {
        g_flow_control_active = true;
        g_flow_control_events++;
      
        
        // 发送流控消息到MQTT（可选）
        if (g_mqtt_client && g_mqtt_connected) {
            esp_mqtt_client_publish(g_mqtt_client, "topic/flow_control", 
                                   "SLOW", 4, 0, 0);
        }
    }
    
    return true;
}

// 从环形缓冲读取数据（线程安全）
static size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *data, size_t max_len, size_t *offset) {
    if (max_len == 0 || rb->count == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(rb->mutex, 0) != pdTRUE) {
        return 0;
    }
    
    // 计算实际可读长度
    size_t read_len = (rb->count < max_len) ? rb->count : max_len;
    if (read_len == 0) {
        xSemaphoreGive(rb->mutex);
        return 0;
    }
    
    // 记录读取起始位置
    if (offset) {
        *offset = rb->tail;
    }
    
    // 读取数据（处理回绕）
    size_t read_pos = rb->tail;
    for (size_t i = 0; i < read_len; i++) {
        data[i] = rb->buffer[read_pos];
        read_pos = (read_pos + 1) % RING_BUFFER_SIZE;
    }
    
    // 更新指针和计数
    rb->tail = read_pos;
    rb->count -= read_len;
    
    // 检查是否可以取消流控
    bool can_resume_flow = (rb->count < (RING_BUF_THRESHOLD / 2)) && g_flow_control_active;
    
    xSemaphoreGive(rb->mutex);
    
    // 恢复流控（在锁外执行）
    if (can_resume_flow) {
        g_flow_control_active = false;
        
        
        if (g_mqtt_client && g_mqtt_connected) {
            esp_mqtt_client_publish(g_mqtt_client, "topic/flow_control", 
                                   "FAST", 4, 0, 0);
        }
    }
    
    return read_len;
}

// 获取当前数据量（线程安全）
static size_t ring_buffer_count(ring_buffer_t *rb) {
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    size_t count = rb->count;
    xSemaphoreGive(rb->mutex);
    return count;
}


static void serial_tx_task(void *arg) {
    tx_queue_item_t tx_item;
    
    
    
    for (;;) {
        // 先尝试从环形缓冲读取
        size_t read_len = ring_buffer_read(&g_ring_buffer, tx_item.data, 
                                           sizeof(tx_item.data), NULL);
        if (read_len > 0) {
            tx_item.len = read_len;
            
            // 尝试放入发送队列
            if (xQueueSend(g_serial_tx_queue, &tx_item, 0) != pdTRUE) {
                // 队列满，直接发送
                uart_write_bytes(UART_NUM_0, (const char*)tx_item.data, read_len);
                g_serial_sent++;
                g_queue_dropped++;
            }
        }
        
        // 处理队列中的数据
        if (xQueueReceive(g_serial_tx_queue, &tx_item, pdMS_TO_TICKS(10)) == pdTRUE) {
            int written = uart_write_bytes(UART_NUM_0, (const char*)tx_item.data, tx_item.len);
            if (written > 0) {
                g_serial_sent++;
            }
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}
void serial_init(void)
{
    if (s_serial_mux == NULL) {
        s_serial_mux = xSemaphoreCreateMutex();
    }
   
}

int serial_write_bytes(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return 0;
    if (s_serial_mux) xSemaphoreTake(s_serial_mux, portMAX_DELAY);
    ssize_t r = write(1, data, len); // 写到 stdout -> VFS -> UART
    if (s_serial_mux) xSemaphoreGive(s_serial_mux);
    if (r < 0) {
        //ESP_LOGE(TAG, "write() failed");
        return -1;
    }
    return (int)r;
}

int serial_write_byte(uint8_t b)
{
    return serial_write_bytes(&b, 1);
}

int mqtt_publish_bytes(esp_mqtt_client_handle_t client,
                       const char *topic,
                       const uint8_t *data,
                       int len,
                       int qos,
                       int retain)
{
    if (!client || !topic) return -1;
    return esp_mqtt_client_publish(client, topic, (const char*)data, len, qos, retain);
}

int mqtt_publish_printf(esp_mqtt_client_handle_t client,
                        const char *topic,
                        int qos,
                        int retain,
                        const char *fmt, ...)
{
    if (!client || !topic || !fmt) return -1;

    int needed;
    va_list ap;
    va_start(ap, fmt);
    needed = vsnprintf(NULL, 0, fmt, ap); // 获取需要的长度
    va_end(ap);
    if (needed < 0) return -1;

    char *buf = malloc((size_t)needed + 1);
    if (!buf) return -1;

    va_start(ap, fmt);
    vsnprintf(buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    int msg_id = esp_mqtt_client_publish(client, topic, buf, needed, qos, retain);
    free(buf);
    return msg_id; // >0 成功的 msg id, -1 失败
}

void serial_register_rx_cb(void (*cb)(const uint8_t *data, size_t len, void *ctx), void *ctx) {
    rx_cb = cb;
    rx_cb_ctx = ctx;
}

static void uart_rx_task(void *arg)
{
    uart_event_t evt;
    uint8_t *data = malloc(RX_BUF_SIZE);
    if (!data) {
       // ESP_LOGE(TAG, "malloc failed");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        if (xQueueReceive(uart_queue, &evt, portMAX_DELAY)) {
            if (evt.type == UART_DATA) {
                int len = uart_read_bytes(UART_NUM, data, evt.size, pdMS_TO_TICKS(1000));
                if (len > 0) {
                    if (rx_cb) {
                        rx_cb(data, (size_t)len, rx_cb_ctx);
                    } else {
                        // 默认行为：打印到 console（避免递归写）
                        //(TAG, "RX %d bytes", len);
                    }
                }
            } else if (evt.type == UART_FIFO_OVF) {
               // ESP_LOGW(TAG, "UART FIFO OVF");
                uart_flush_input(UART_NUM);
                xQueueReset(uart_queue);
            } else if (evt.type == UART_BUFFER_FULL) {
               // ESP_LOGW(TAG, "UART buffer full");
                uart_flush_input(UART_NUM);
                xQueueReset(uart_queue);
            }
            // 处理其它事件（UART_BREAK, PARITY_ERR 等）如需
        }
    }
    free(data);
    vTaskDelete(NULL);
}
static void my_rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    if (!data || len == 0) return;
    buf_t *b = buf_alloc(0);
    if (!b) {
       // ESP_LOGW(TAG, "buf_alloc failed, drop RX");
        return;
    }
    size_t copy_len = (len > sizeof(b->data)) ? sizeof(b->data) : len;
    memcpy(b->data, data, copy_len);
    b->len = copy_len;
    if (b->len < BUF_SIZE) b->data[b->len] = '\0';
    if (xQueueSend(publish_queue, &b, 0) != pdTRUE) {
      //  ESP_LOGW(TAG, "publish_queue full, drop RX");
        buf_queue_drop_count++;
        buf_free(b);
    }
} 

static void mqtt_publish_task(void *arg)
{
    buf_t *msg;
    for (;;) {
        if (xQueueReceive(publish_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!g_mqtt_client || !g_mqtt_connected) {
              //  ESP_LOGW(TAG, "MQTT not connected, drop msg");
                buf_free(msg);
                continue;
            }

            if (msg->len == 0) {
                buf_free(msg);
                continue;
            }

            uint8_t id_char = msg->data[0];
            const char *topic = "topic/state";
            switch (id_char) {
                case '0': topic = "topic/map_data"; break;
                case '1': topic = "topic/state"; break;
                case '2': topic = "topic/getted"; break;
                case '3': topic = "topic/speed"; break;
                default: topic = "topic/state"; break;
            }

            int payload_len = (msg->len > 1) ? (int)(msg->len - 1) : 0;
            const char *payload = (const char *)(msg->data + 1);
            int msg_id = -1;
            if (payload_len > 0) {
                msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, payload, payload_len, 0, 0);
            } else {
                msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, NULL, 0, 0, 0);
            }
            if (msg_id == -1) 
            serial_write_bytes((const uint8_t*)"0\n" , strlen("0\n"));
          
            //ESP_LOGW(TAG, "publish to %s failed", topic);
            //ESP_LOGI(TAG, "published to %s, payload_len=%d, msgid=%d", topic, payload_len, msg_id);

            // human friendly note on serial
            // serial_write_bytes((const uint8_t*)"已向主题 ", strlen("已向主题 "));
            // serial_write_bytes((const uint8_t*)topic, strlen(topic));
            // serial_write_bytes((const uint8_t*)" 发送数据\n", strlen(" 发送数据\n"));

            buf_free(msg);
        }
    }
    vTaskDelete(NULL);
}

void serial_rx_init(void)
{
    uart_config_t cfg = {
        .baud_rate = CONFIG_CONSOLE_UART_BAUDRATE, // 或自定义
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &cfg);
    // 默认 UART0 TX=GPIO1, RX=GPIO3；如果需要重映射，可以使用 uart_set_pin
    // uart_set_pin(UART_NUM, TX_pin, RX_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // 安装驱动，rx buffer = 2 * RX_BUF_SIZE, tx buffer = 0, evt queue length, flags=0
    uart_driver_install(UART_NUM, RX_BUF_SIZE * 2, 0, UART_EVT_QUEUE_LEN, &uart_queue, 0);

    // 创建 RX 任务
    xTaskCreate(uart_rx_task, "uart_rx_task", RX_TASK_STACK, NULL, configMAX_PRIORITIES - 1, NULL);
}
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{


    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
       
        case MQTT_EVENT_CONNECTED:
            //ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            g_mqtt_connected = true;
            // msg_id = esp_mqtt_client_publish(client, "topic/qos1", "data_3", 0, 1, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            // msg_id = esp_mqtt_client_subscribe(client, "topic/state", 0);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            // msg_id = esp_mqtt_client_subscribe(client, "topic/locate", 0);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            // msg_id = esp_mqtt_client_subscribe(client, "topic/map_data", 0);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
             msg_id = esp_mqtt_client_subscribe(client, "topic/speed", 1);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
             msg_id = esp_mqtt_client_subscribe(client, "topic/machion_arm", 1);
            //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
             msg_id = esp_mqtt_client_subscribe(client, "topic/doing", 1);
           // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
             //msg_id = esp_mqtt_client_publish(client, "topic/state","1000004",0,0,0);
             printf("88888\n"); ; 
             //在此处发布一条上电消息，表示成功联网,向主机发送状态改变信息 ； 
            break;
/*
            msg_id = esp_mqtt_client_unsubscribe(client, "topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break; //取消订阅
*/
       case MQTT_EVENT_DISCONNECTED:
            g_mqtt_connected = false;
            //ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            //ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
           
            //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);//订阅成功后发送一条消息
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
           // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
       case MQTT_EVENT_DATA: {
            g_mqtt_received++;
            
            /* Per-topic processing: add a topic-specific prefix */
            const char *prefix = "";
            if (event->topic_len == (int)strlen("topic/map_data") && memcmp(event->topic, "topic/map_data", event->topic_len) == 0) {
                prefix = "3";
            } else if (event->topic_len == (int)strlen("topic/doing") && memcmp(event->topic, "topic/doing", event->topic_len) == 0) {
                prefix = "2";
            } else if (event->topic_len == (int)strlen("topic/machion_arm") && memcmp(event->topic, "topic/machion_arm", event->topic_len) == 0) {
                prefix = "1";
            } else if (event->topic_len == (int)strlen("topic/speed") && memcmp(event->topic, "topic/speed", event->topic_len) == 0) {
                prefix = "0";
            }

            const uint8_t *payload = (const uint8_t *)event->data;
            int payload_len = event->data_len;
            
            // 计算总长度
            size_t prefix_len = strlen(prefix);
            size_t total_len = prefix_len + payload_len;
            
            // 限制最大长度（根据后端约定）
            if (total_len > 128) {
                // 如果超过100字节，截断payload
                if (payload_len > (128 - prefix_len)) {
                    payload_len = 128 - prefix_len;
                    total_len = 128;
                    ESP_LOGW(TAG, "数据包超过128字节限制，已截断");
                }
            }
            
            // 准备写入环形缓冲的数据
            uint8_t out_buf[130];  // 小缓冲区，在栈上分配
            size_t out_len = 0;
            
            // 复制前缀
            memcpy(out_buf, prefix, prefix_len);
            out_len += prefix_len;
            
            // 复制payload
            if (payload_len > 0) {
                memcpy(out_buf + prefix_len, payload, payload_len);
                out_len += payload_len;
            }
            out_buf[out_len++] = '\n';
            // 写入环形缓冲
            if (!ring_buffer_write(&g_ring_buffer, out_buf, out_len)) {
              // ESP_LOGW(TAG, "failed to write to ring buffer,lost (size:%d)", out_len);
            }
            
            // 每收到100个包打印一次统计
            if (g_mqtt_received % 100 == 0) {
                size_t rb_count = ring_buffer_count(&g_ring_buffer);
                // ESP_LOGI(TAG, "MQTT接收统计: 总数=%lu, 环形缓冲使用=%d/%d", 
                //         g_mqtt_received, rb_count, RING_BUFFER_SIZE);
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            //ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
       
       
        default:
            //ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
   
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
       // printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
       // printf("Broker url: %s\n", line);
    } else {
       // ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

   esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    g_mqtt_client = client;
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

}


void mtqq_start(void)
{
     
   // ESP_LOGI(TAG, "[APP] Startup..");
   // ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
   // ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    // esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    // esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    // esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    // esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    // esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

  
    ESP_ERROR_CHECK(example_connect());

}

void app_main(void) {
    
    // 初始化串口
    serial_init();
    serial_rx_init();
    
    // 初始化环形缓冲区
    ring_buffer_init(&g_ring_buffer);
   
    
    // 创建串口发送队列
    g_serial_tx_queue = xQueueCreate(SERIAL_TX_QUEUE_SIZE, sizeof(tx_queue_item_t));
   
    
    // 创建串口发送任务
    BaseType_t task_created = xTaskCreate(
        serial_tx_task,         
        "serial_tx",             
        4096,                    
        NULL,                   
        tskIDLE_PRIORITY + 5,    
        &g_serial_tx_task       
    );
    
    
  
    bufpool_init();
    publish_queue = xQueueCreate(30, sizeof(buf_t *));
    xTaskCreate(mqtt_publish_task, "mqtt_publish", 4096, NULL, tskIDLE_PRIORITY + 5, NULL);
    
    // 注册串口接收回调
    serial_register_rx_cb(my_rx_cb, NULL);
    
    // 启动MQTT
    mtqq_start();
    mqtt_app_start();
    
   
    
}