/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#define TICK_INT_PRIORITY 0x0F

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED

/* ?????? */
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

/* ?????????? */
// #define HAL_ADC_MODULE_ENABLED
// #define HAL_CAN_MODULE_ENABLED
// #define HAL_CRC_MODULE_ENABLED
// #define HAL_CRYP_MODULE_ENABLED
// #define HAL_DAC_MODULE_ENABLED
// #define HAL_DCMI_MODULE_ENABLED
// #define HAL_DMA2D_MODULE_ENABLED
// #define HAL_ETH_MODULE_ENABLED
// #define HAL_I2C_MODULE_ENABLED
// #define HAL_I2S_MODULE_ENABLED
// #define HAL_TIM_MODULE_ENABLED
// #define HAL_USART_MODULE_ENABLED
// #define HAL_UART_MODULE_ENABLED
// #define HAL_LTDC_MODULE_ENABLED
// ... ?????????

/* ########################## HSE/HSI Values ############################### */
#if !defined(HSE_VALUE)
  #define HSE_VALUE    25000000U
#endif

#if !defined(HSI_VALUE)
  #define HSI_VALUE    16000000U
#endif

#if !defined(LSI_VALUE)
  #define LSI_VALUE    32000U
#endif

#if !defined(LSE_VALUE)
  #define LSE_VALUE    32768U
#endif

#if !defined(EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE    12288000U
#endif

/* ########################### System Configuration ######################### */
#ifndef VDD_VALUE
#define  VDD_VALUE  3300U
#endif

#ifndef PREFETCH_ENABLE
#define  PREFETCH_ENABLE  1U
#endif

#ifndef INSTRUCTION_CACHE_ENABLE
#define  INSTRUCTION_CACHE_ENABLE  1U
#endif

#ifndef DATA_CACHE_ENABLE
#define  DATA_CACHE_ENABLE 1U
#endif

/* ########################## Includes ###################################### */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
  #include "stm32f4xx_hal_exti.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif

/* ########################## Assert Selection ############################## */
#define USE_FULL_ASSERT 1U
#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
