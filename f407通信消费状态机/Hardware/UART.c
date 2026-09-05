#include "stm32f4xx.h"                  // Device header
#include "UART.h"
#include "MY_Machine_Arm.h"
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_usart.h"
#include "delay.h"
static uint8_t time = 0 ; 
uint16_t speed[4] = { 0 } ; 
#ifdef __GNUC__
    int _write(int file, char *ptr, int len)
    {
        for(int i = 0; i < len; i++)
        {
            while(!LL_USART_IsActiveFlag_TXE(USART2));
            LL_USART_TransmitData8(USART2, ptr[i]);
        }
        return len;
    }
#else
  
    int fputc(int ch, FILE *f)
    {
        while(!LL_USART_IsActiveFlag_TXE(USART2));
        LL_USART_TransmitData8(USART2, (uint8_t)ch);
        return ch;
    }
#endif


#ifndef USART4

#define USART4_BASE           (APB1PERIPH_BASE + 0x00004C00UL)
#define USART4                ((USART_TypeDef *)USART4_BASE)
#endif

#define UART4_PORT GPIOC
#define UART4_RX LL_GPIO_PIN_11
#define UART4_TX LL_GPIO_PIN_10

#define UART2_PORT GPIOA
#define UART2_TX LL_GPIO_PIN_2
#define UART2_RX LL_GPIO_PIN_3

/* 指向主程序中的总缓冲池，用于在中断中把数据分发到各个 topic 缓冲池 */
static ALL_DATA *g_all_data = NULL;

/* 通过 UART4 发送任意字节流（给 ESP8266 使用） */
static void uart4_send_bytes(const uint8_t *buf, uint8_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }
    for (uint8_t i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(USART4));
        LL_USART_TransmitData8(USART4, buf[i]);
    }
}




/* 把一帧完整数据根据 buffer[0] 的 topic id 分发到对应缓冲池，并更新 length/ready 标志 */
static void MQTT_HandleFrame(ALL_DATA *all_data, uint8_t *buf, uint8_t len)
{
  
    if (all_data == NULL || buf == NULL || len == 0)
    {
        return;
    }

    uint8_t topic = buf[0];
    /* 如果是 ASCII '0'~'9'，转换成数值 0~9 */
    if (topic >= '0' && topic <= '9')
    {
        topic = (uint8_t)(topic - '0');
    }

//    /* 通用调试：打印原始帧内容（ASCII 形式） */
//    printf("\r\n[UART4 RX] raw frame, len=%d, first_byte=%d:\r\n", len, (int)buf[0]-'0');
//    for (uint8_t i = 0; i < len; i++)
//    {
//        uint8_t ch = buf[i];
//        if (ch >= 32 && ch <= 126)
//        {
//            printf("%c", ch);
//        }
//        else
//        {
//            printf("%d", ch);
//        }
//    }
   // printf("\r\n");

    switch (topic)
    {

        case Received_TOPIC_ID_SPEED:
        {
           
            Speed_Data *dst = &all_data->speed_data;
            uint8_t copy_len = (len > sizeof(dst->Speed)) ? sizeof(dst->Speed) : len;
            memcpy(dst->Speed, buf, copy_len);
            dst->Speed_Length = copy_len;
            dst->Is_Ready = 1;
            Topic_Speed_Processor(all_data) ; 
             
            //printf("[MQTT BUF] SPEED_F updated, len=%d\r\n", (int)copy_len);
           
            time++ ; 
           // printf("times = %d\n ， count = %d" , time , all_data->speed_data.count ) ; 
//            while( i < all_data->speed_data.Speed_Length)
//            {
//                printf("%x ", all_data->speed_data.Speed[i] ) ;
//                i ++ ; 
//            }
            break;
        }
       
        
         case Received_TOPIC_ID_MACHION_ARM:
        {
            Machion_Arm_Data *dst = &all_data->machion_arm_data;
            uint8_t copy_len = (len > sizeof(dst->machion_arm_data)) ? sizeof(dst->machion_arm_data) : len;
            memcpy(dst->machion_arm_data, buf, copy_len);
            dst->machion_arm_data_len = copy_len;
            dst->Is_Ready = 1;
           // printf("[MQTT BUF] MACHION_ARM updated, len=%d\r\n", (int)copy_len);
            break;
        }
        
           case Received_TOPIC_ID_DOING:
        {
//            STATE_DATA *dst = &all_data->state_data;
            Change_State(all_data , buf[1]) ; 
//            switch (dst->state)
//            {
//                case SPARE : 
//                    printf("SPARE\n") ; 
//                break ; 
//                
//                case WORKING : 
//                     printf("WORKING\n") ; 
//                break ; 
//                
//                case FAULT : 
//                     printf("FAULT\n") ; 
//                break ; 
//                
//                case POWEROFF : 
//                      printf("POWEROFF\n") ; 
//                break ; 
//                
//                case ENERGIZED :
//                       printf("ENERGIZED\n") ; 
//                break ; 
//            }
            break;
        }
        
         case Received_TOPIC_ID_WIFI : 
       {
           STATE_DATA *dst = &all_data->state_data;
            int i = 1 ; 
           while(i < 5)
           {
            if(buf[i] != '8')
            {
                return  ; 
            }
            i++ ; 
           }
           dst->wifi_state = CONNECTED ; 
           //printf("wifi connected\n") ; 
           break ; 
       }
        default:
           
            //printf("[MQTT BUF] UNKNOWN topic=%d, len=%d\r\n", (int)topic, (int)len);
            break;
            
    }
}



void UART4_Init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_UART4);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC) ;
    
    
    LL_GPIO_InitTypeDef UART4_GPIO_InitStructure = {0};
    
    UART4_GPIO_InitStructure.Alternate = LL_GPIO_AF_8;
    UART4_GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    UART4_GPIO_InitStructure.Pin = UART4_RX | UART4_TX;
    UART4_GPIO_InitStructure.Pull = LL_GPIO_PULL_UP;
    UART4_GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    UART4_GPIO_InitStructure.Mode = LL_GPIO_MODE_ALTERNATE;
    LL_GPIO_Init(UART4_PORT , &UART4_GPIO_InitStructure) ;

    LL_USART_Disable(USART4) ;
    LL_USART_InitTypeDef USART4_InitStructure = {0} ;
    
    USART4_InitStructure.BaudRate = 921600;
    USART4_InitStructure.DataWidth = LL_USART_DATAWIDTH_8B;
    USART4_InitStructure.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    USART4_InitStructure.OverSampling = LL_USART_OVERSAMPLING_16;
    USART4_InitStructure.Parity = LL_USART_PARITY_NONE;
    USART4_InitStructure.StopBits = LL_USART_STOPBITS_1;
    USART4_InitStructure.TransferDirection = LL_USART_DIRECTION_TX_RX;
    
    LL_USART_Init(USART4 , &USART4_InitStructure) ;
    LL_USART_Enable(USART4) ;
    
    /* 使能接收中断并配置 NVIC */
    LL_USART_EnableIT_RXNE(USART4);
    NVIC_SetPriority(UART4_IRQn, 1);
    NVIC_EnableIRQ(UART4_IRQn);
    
}



void UART2_Init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    LL_GPIO_SetPinMode(UART2_PORT, UART2_RX, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinMode(UART2_PORT, UART2_TX, LL_GPIO_MODE_ALTERNATE);

    LL_GPIO_SetAFPin_0_7(UART2_PORT, UART2_RX, LL_GPIO_AF_7);
    LL_GPIO_SetAFPin_0_7(UART2_PORT, UART2_TX, LL_GPIO_AF_7);

    LL_GPIO_SetPinSpeed(UART2_PORT, UART2_RX, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinSpeed(UART2_PORT, UART2_TX, LL_GPIO_SPEED_FREQ_HIGH);

    LL_GPIO_SetPinPull(UART2_PORT, UART2_RX, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinPull(UART2_PORT, UART2_TX, LL_GPIO_PULL_UP);

    LL_USART_Disable(USART2);
    LL_USART_InitTypeDef USART_InitStructure = {0} ;
    USART_InitStructure.BaudRate = 115200;
    USART_InitStructure.DataWidth = LL_USART_DATAWIDTH_8B;
    USART_InitStructure.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    USART_InitStructure.OverSampling = LL_USART_OVERSAMPLING_16;
    USART_InitStructure.Parity = LL_USART_PARITY_NONE;
    USART_InitStructure.StopBits = LL_USART_STOPBITS_1;
    USART_InitStructure.TransferDirection = LL_USART_DIRECTION_TX_RX;
    
    LL_USART_Init(USART2, &USART_InitStructure);
    LL_USART_Enable(USART2);
}


/* UART4 接收中断处理：组帧后分发到各个 MQTT 主题缓冲池 */
void UART4_IRQHandler(void)
{
    static uint8_t rx_buf[254];
    static uint8_t rx_len = 0;
 
    if (LL_USART_IsActiveFlag_RXNE(USART4))
    {
        uint8_t data = LL_USART_ReceiveData8(USART4);

        if (rx_len < sizeof(rx_buf))
        {
            rx_buf[rx_len++] = data;
        }

        /* 收到换行或缓冲满时认为一帧结束，交给缓冲池处理 */
        if (data == '\n' || rx_len >= sizeof(rx_buf))
        {
            if (g_all_data != NULL)
            {
                MQTT_HandleFrame(g_all_data, rx_buf, rx_len);
            }
            rx_len = 0;
        }
    }
}

void UART_SetAllDataPtr(ALL_DATA *all_data)
{
    g_all_data = all_data;
}

void MQTT_Send_Data(ALL_DATA* all_data)
{
    if (all_data == NULL)
    {
        return;
    }

    /* 依次检查所有缓冲池，如果 ready 且 length>0，就通过 UART4 发送并清空 */

    if (all_data->test1_data.Is_Ready && all_data->test1_data.test_data_len > 0)
    {
        uart4_send_bytes(all_data->test1_data.test_data, all_data->test1_data.test_data_len);
        all_data->test1_data.test_data_len = 0;
        all_data->test1_data.Is_Ready = 0;
    }

   

    if (all_data->map_data.Is_Ready && all_data->map_data.Data_Length > 0)
    {
        uart4_send_bytes(all_data->map_data.map_Data, all_data->map_data.Data_Length);
        all_data->map_data.Data_Length = 0;
        all_data->map_data.Is_Ready = 0;
    }

   
    if (all_data->state_data.Is_Ready)
    {
        all_data->state_data.state_data[0] = 1 + '0' ; 
        all_data->state_data.state_data[1] = ONLY_ADDRESS ; 
        all_data->state_data.state_data[2] = all_data->state_data.RTX*100 - ((int)(all_data->state_data.RTX)*100) ; 
        all_data->state_data.state_data[3] = (int) all_data->state_data.RTX ; 
        all_data->state_data.state_data[4] = all_data->state_data.RTY*100 - ((int)(all_data->state_data.RTY)*100)  ; 
        all_data->state_data.state_data[5] = (int) all_data->state_data.RTY ; 
        all_data->state_data.state_data[6] = (int) all_data->state_data.RTPOWER ; 
        all_data->state_data.state_data[7] =  all_data->state_data.state; 
        
        uart4_send_bytes(all_data->state_data.state_data, 8);
        all_data->state_data.Is_Ready = 0 ; 
    }
    
    if(all_data->geted_struct.Is_Ready && all_data->geted_struct.Geted_Data_Len > 0)
    {
        uart4_send_bytes(all_data->geted_struct.Geted_Data , all_data->geted_struct.Geted_Data_Len) ;
        all_data->geted_struct.Geted_Data_Len = 0 ; 
        all_data->geted_struct.Is_Ready = 0 ; 
    }
    
}
   
void Topic_Speed_Processor(ALL_DATA *all_data)
{
    Speed_Data *sd = &all_data->speed_data;

    for (int group = 0; group < 10; group++) 
    {  
        int base_idx = group * 8 + 1;  
        
       
        uint16_t val0 = (sd->Speed[base_idx] << 8) | sd->Speed[base_idx + 1];
        uint16_t val1 = (sd->Speed[base_idx + 2] << 8) | sd->Speed[base_idx + 3];
        uint16_t val2 = (sd->Speed[base_idx + 4] << 8) | sd->Speed[base_idx + 5];
        uint16_t val3 = (sd->Speed[base_idx + 6] << 8) | sd->Speed[base_idx + 7];

       
       
        sd->ring_buffer[sd->write_index].w0 = val0;
        sd->ring_buffer[sd->write_index].w1 = val1;
        sd->ring_buffer[sd->write_index].w2 = val2;
        sd->ring_buffer[sd->write_index].w3 = val3;

       
        sd->write_index = (sd->write_index + 1) % SPEED_SAMPLES;

        
        if (sd->count < SPEED_SAMPLES) 
        {
            sd->count++;
        } 
        else 
        {
          
            sd->read_index = (sd->read_index + 1) % SPEED_SAMPLES;
        }
    }
}


void Get_Speed(ALL_DATA * all_data, uint16_t speed[4])
{
    // 开启中断锁
    uint32_t primask = __get_PRIMASK();
    __disable_irq(); 

    if (all_data->speed_data.count > 0) 
    {
        Speed_Data *sd = &all_data->speed_data;
        speed[0] = sd->ring_buffer[sd->read_index].w0;
        speed[1] = sd->ring_buffer[sd->read_index].w1;
        speed[2] = sd->ring_buffer[sd->read_index].w2;
        speed[3] = sd->ring_buffer[sd->read_index].w3;

        sd->read_index = (sd->read_index + 1) % SPEED_SAMPLES;
        sd->count--;
    }

    // 恢复中断
    __set_PRIMASK(primask);
}





void State_Init(ALL_DATA * all_data)
{
    all_data->state_data.RTX = 0 ;
    all_data->state_data.RTY = 0 ; 
    all_data->state_data.state = ENERGIZED ; 
    all_data->state_data.RTPOWER = realpower ; 
    all_data->state_data.Is_Ready = 0 ; 
    all_data->state_data.goods_state = GETING ; 
}

void Change_State(ALL_DATA * all_data , MACHION_STATE machion_state)
{
    all_data->state_data.state = machion_state ; 
}

void Change_State_Data(ALL_DATA * all_data , double RTX , 
                            double RTY , double RTP )
{
    all_data->state_data.RTPOWER = RTP ; 
    all_data->state_data.RTX = RTX ; 
    all_data->state_data.RTY = RTY ; 
    
}

void Ready_To_Send_State_Data(ALL_DATA * all_data )
{
    all_data->state_data.Is_Ready = 1 ; 
}


uint8_t  Get_CUrrent_State(ALL_DATA * all_data)
{
    return all_data->state_data.state ; 
}

void ENERGIZED_PUBLISH(ALL_DATA * all_data)
{
   
    while(!all_data->state_data.wifi_state)
    {
    
    }
    Ready_To_Send_State_Data(all_data) ; 
    MQTT_Send_Data(all_data) ; 
    delay_ms(1000) ; 
    return ; 
}


void Send_Geted(ALL_DATA * all_data )
{
    all_data->geted_struct.Geted_Data[0] = Send_TOPIC_ID_GETTED + '0'; 
    all_data->geted_struct.Geted_Data[1] = 1 ; 
    all_data->geted_struct.Geted_Data_Len = 2 ; 
    all_data->geted_struct.Is_Ready = 1 ; 
    MQTT_Send_Data(all_data) ; 
}


static void State_Spare_Handler(ALL_DATA * all_data)
{
    Ready_To_Send_State_Data(all_data) ; 
    MQTT_Send_Data(all_data) ; 
}

// 复位上下文环境
void Reset_Context(CycleContext *ctx) {
    ctx->op_cnt = 0;
    ctx->zero_cnt = 0;
    ctx->possibly_done = false;
    ctx->history_idx = 0;
    // 将历史记录初始化为无效值，避免一启动就误判为稳定
    for(int i=0; i<3; i++) ctx->history[i] = 0xFFFF; 
}


bool Process_Production_Logic(ALL_DATA *all_data, CycleContext *ctx) {
    uint16_t count = all_data->speed_data.count;
    ctx->op_cnt++;
    
    // 更新环形缓冲区
    ctx->history[ctx->history_idx] = count;
    ctx->history_idx = (ctx->history_idx + 1) % 3;
    
    // 执行消费逻辑 (假设 speed 是你定义好的缓冲区)
    Get_Speed(all_data,speed); 

    if (count == 0) {
        ctx->zero_cnt++;
        
        // 稳定性判断：最近3次采集到的 count 是否完全一致且为0
        bool is_stable = (ctx->history[0] == 0 && 
                          ctx->history[1] == 0 && 
                          ctx->history[2] == 0);
        
        if (ctx->zero_cnt >= ZERO_STABILITY_THRESHOLD && 
            is_stable && 
            ctx->op_cnt > NORMAL_OPERATION_THRESHOLD) {
            return true; // 判定为生产结束
        }
        ctx->possibly_done = true;
    } else {
        // 只要 count 不为 0，说明还在生产，重置零计数
        ctx->zero_cnt = 0;
        ctx->possibly_done = false;
    }
    return false;
}

static void State_Working_Handler(ALL_DATA * all_data)
{
    // 两个状态各自独立的上下文
    static CycleContext getting_ctx = {0};
    static CycleContext getted_ctx = {0};
    static bool Ready_To_Go = false;
    
    // 1. 前置检查：等待初始数据量
    if(!Ready_To_Go)
    {
        if(all_data->speed_data.count < aim_num) return; 
        
        Ready_To_Go = true;
        Reset_Context(&getting_ctx); // 准备进入 GETTING
        return;
    }
    
    // 2. 状态机逻辑
    switch (all_data->state_data.goods_state)
    {
        case GETING:
            if (Process_Production_Logic(all_data, &getting_ctx)) 
            {
             //   printf("uturn\n"); // 触发 U-Turn 动作
                all_data->state_data.goods_state = GETED;
                Machine_Arm_Init(1000) ; 
                delay_ms(2000) ; 
                Machine_Arm_Left_Grab(1000) ;
                delay_ms(2000) ; 
                Machine_Arm_Left_Reset(2000) ; 
                 Send_Geted(all_data); 
                Reset_Context(&getted_ctx); // 初始化下一个状态的上下文
            }
            break;
            
        case GETED:
            // GETED 状态特有动作：申请再次生产
            static bool has_requested = false;
//            if (!has_requested) {
//                printf("getted: requesting more data...\n");
//                Send_Geted(all_data); 
//                has_requested = true;
//            }

            if (Process_Production_Logic(all_data, &getted_ctx)) 
            {
              //  printf("uturn\n"); // 再次触发 U-Turn
               
                has_requested = false; 
                 all_data->state_data.state = SPARE; 
            }
            break;
    }
}

static void State_Fault_Handler(ALL_DATA * all_data)
{
  //  printf("iam fault \n") ; 
}




static void State_PowerOff_Handler(ALL_DATA * all_data)
{
   // printf("iam poweroff \n") ; 
}




static void State_Energized_Handler(ALL_DATA * all_data)
{
    //  printf("iam energized \n") ; 
}




void State_Handle_Machion(ALL_DATA * all_data)
{
    uint8_t current_state = all_data->state_data.state ; 
    switch (current_state)
    {
        case SPARE :
           State_Spare_Handler(all_data) ; 
        break ; 

        case WORKING :
           State_Working_Handler(all_data) ; 
        break ; 

        case FAULT :
            State_Fault_Handler(all_data) ; 
        break ; 

        case POWEROFF :
            State_PowerOff_Handler(all_data) ; 
        break ; 

        case ENERGIZED :
          State_Energized_Handler(all_data) ; 
        break ; 
    }
}

