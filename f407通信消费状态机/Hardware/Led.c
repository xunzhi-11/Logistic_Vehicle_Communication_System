#include "stm32f4xx.h"                  // Device header
#include "Led.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
void Led_Init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
    LL_GPIO_InitTypeDef  GPIO_InitStrucuture = {0} ;
    GPIO_InitStrucuture.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStrucuture.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStrucuture.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStrucuture.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStrucuture.Pin = LL_GPIO_PIN_11; 
    LL_GPIO_Init(GPIOD , &GPIO_InitStrucuture) ;
}
