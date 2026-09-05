#ifndef __UART_H
#define __UART_H
#include "stdbool.h"
#define map_data_buffer_size 255
#define machion_arm_buffer_size 100
#define speed_buffer_size 255
#define ONLY_ADDRESS 1
#define realpower 100
#define rtx 100
#define rty 100
#define SPEED_SAMPLES 1000
#define aim_num 390
#define aim_num_geted 200
#define ZERO_STABILITY_THRESHOLD   100  // 连续检测到0的次数阈值
#define NORMAL_OPERATION_THRESHOLD 500  // 正常操作期间的调用次数阈值
#define CHANGE_DETECTION_THRESHOLD 5   // 变化检测阈值
//缓冲池及数据集结构体
typedef enum
{
    
    Received_TOPIC_ID_SPEED   = 0 ,  
    Received_TOPIC_ID_MACHION_ARM = 1 ,
    Received_TOPIC_ID_DOING = 2 , 
    Received_TOPIC_ID_WIFI = 8
} MQTT_Received_Topic_Id;

typedef enum
{
    
    Send_TOPIC_ID_MAP_DATA   = 0 ,     
    Send_TOPIC_ID_STATE = 1 , 
    Send_TOPIC_ID_GETTED = 2
    
} MQTT_Send_Topic_Id;


typedef enum 
{
    SPARE = 0 ,  
    WORKING = 1 , 
    FAULT = 2 , 
    POWEROFF = 3 ,
    ENERGIZED = 4
} MACHION_STATE; 

typedef enum
{
    DISCONNECTED = 0  , 
    CONNECTED  = 1
} WIFI_STATE; 

typedef enum
{
    GETING = 0 ,
    GETED = 1
} GOODS_STATE;

typedef struct {
    uint32_t op_cnt;
    uint32_t zero_cnt;
    uint16_t history[3];
    uint8_t history_idx;
    bool possibly_done;
} CycleContext;



typedef struct
{
    uint8_t map_Data[map_data_buffer_size] ; 
    uint8_t Data_Length;           
    uint8_t Is_Ready ;             
   
}  Map_Data; 


typedef struct {
    uint16_t w0, w1, w2, w3;
} Speed_Sample_t;

typedef struct
{
    uint8_t Speed[speed_buffer_size];
    uint8_t Speed_Length;       
    volatile uint8_t Is_Ready; 
    Speed_Sample_t ring_buffer[SPEED_SAMPLES];
  
    volatile uint16_t read_index;  
    volatile uint16_t write_index; 
    volatile uint16_t count;     
} Speed_Data;

typedef struct 
{
    double RTX; 
    double RTY; 
    double RTPOWER; 
    MACHION_STATE state; 
    uint8_t Is_Ready; 
    uint8_t state_data[8]; 
    WIFI_STATE wifi_state ; 
    GOODS_STATE goods_state ; 
} STATE_DATA; 


typedef struct 
{
    uint8_t machion_arm_data[machion_arm_buffer_size] ; 
    uint8_t machion_arm_data_len ;     
    uint8_t Is_Ready ;                  
} Machion_Arm_Data; 

typedef struct
{
    uint8_t Geted_Data[2] ; 
    uint8_t Is_Ready ; 
    uint8_t Geted_Data_Len ; 
} GETED_STRUCT ; 


typedef struct 
{
    uint8_t test_data[100] ; 
    uint8_t test_data_len ;     
    uint8_t Is_Ready ;          
} Test_Data; 

typedef struct
{
    Map_Data map_data ; 
    Speed_Data speed_data ; 
    Test_Data test1_data ; 
    STATE_DATA state_data ; 
    Machion_Arm_Data machion_arm_data ; 
    GETED_STRUCT geted_struct ; 
} ALL_DATA; 

//外设处理
void UART4_Init(void) ;

void UART4_IRQHandler(void) ;
void UART_SetAllDataPtr(ALL_DATA *all_data) ;
void UART2_Init(void)  ; 


//状态机接口
void Topic_Speed_Processor(ALL_DATA *all_data) ; 
void MQTT_Send_Data(ALL_DATA* all_data) ; 
void State_Init(ALL_DATA * all_data);
void Change_State(ALL_DATA * all_data, MACHION_STATE machion_state);
void Change_State_Data(ALL_DATA * all_data, double RTX, 
                       double RTY, double RTP);
void Ready_To_Send_State_Data(ALL_DATA * all_data);
void Get_Speed(ALL_DATA * all_data , uint16_t speed[4])  ; 
void ENERGIZED_PUBLISH(ALL_DATA * all_data) ; 
void State_Handle_Machion(ALL_DATA * all_data) ; 
void Send_Geted(ALL_DATA * all_data ) ; 
#endif
