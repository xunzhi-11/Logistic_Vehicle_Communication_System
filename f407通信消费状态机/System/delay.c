#include "stm32f4xx.h"                  // Device header
#include "delay.h"

static uint32_t fac_us = 0;   // 1us 需要的 SysTick tick 数

/**
  * @brief  初始化延时（需在 SystemClock 配置后调用）
  */
void delay_Init(void)
{
    // SysTick 采用 HCLK 时钟源（一般为 168MHz）
    SysTick->CTRL &= ~SysTick_CTRL_CLKSOURCE_Msk;  // 先清除
    SysTick->CTRL |=  SysTick_CTRL_CLKSOURCE_Msk;  // 使用 HCLK

    fac_us = SystemCoreClock / 1000000;            // 每微秒多少 tick
}


/**
  * @brief  微秒级延时
  * @param  us 0~233015（24 位最大 tick 限制）
  */
void delay_us(uint32_t us)
{
    uint32_t ticks = us * fac_us;

    if (ticks > SysTick_LOAD_RELOAD_Msk)  // > 0xFFFFFF
        ticks = SysTick_LOAD_RELOAD_Msk;  // 限制最大值，防止卡死

    SysTick->LOAD = ticks;                // 重装值
    SysTick->VAL  = 0;                    // 清当前计数
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // 启动

    // 等待 COUNTFLAG = 1
    while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // 停止
}


/**
  * @brief  毫秒级延时
  */
void delay_ms(uint32_t ms)
{
    while (ms--)
        delay_us(1000);
}


/**
  * @brief  秒级延时
  */
void delay_s(uint32_t s)
{
    while (s--)
        delay_ms(1000);
}
