#include "stm32f4xx.h"                  // Device header
#include "Key.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
void Key_Init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
    LL_GPIO_InitTypeDef Key_InitStructure = {0} ;
    Key_InitStructure.Mode = LL_GPIO_MODE_INPUT;
    Key_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    Key_InitStructure.Pin = LL_GPIO_PIN_1;
    Key_InitStructure.Pull = LL_GPIO_PULL_NO;
    Key_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    LL_GPIO_Init(GPIOD , &Key_InitStructure);
}

