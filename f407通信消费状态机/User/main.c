#include "stm32f4xx.h"                  // Device header
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "delay.h"
#include "oled.h"
#include "UART.h"
#include "string.h"

extern uint16_t speed[4] ;  
int main(void)
{
    ALL_DATA all_data = {0} ; 
    delay_Init() ;
    UART4_Init() ;
    UART2_Init() ; 
    UART_SetAllDataPtr(&all_data) ;
    State_Init(&all_data) ; 
    
    OLED_Init() ; 
 
    ENERGIZED_PUBLISH(&all_data) ; 
    //all_data.test1_data.Is_Ready = 1 ; 
   
    while(1)
    {
       State_Handle_Machion(&all_data) ; 
  
        delay_ms(20);  
       
    }
}
