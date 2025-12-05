/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_RDY_Pin GPIO_PIN_6
#define LED_RDY_GPIO_Port GPIOA
#define LED_ERR_Pin GPIO_PIN_7
#define LED_ERR_GPIO_Port GPIOA
#define NFC_RST_Pin GPIO_PIN_1
#define NFC_RST_GPIO_Port GPIOB
#define NF__MISO_Pin GPIO_PIN_2
#define NF__MISO_GPIO_Port GPIOB
#define NFC_CLK_Pin GPIO_PIN_10
#define NFC_CLK_GPIO_Port GPIOB
#define NF__MOSI_Pin GPIO_PIN_11
#define NF__MOSI_GPIO_Port GPIOB
#define NF__SS_Pin GPIO_PIN_12
#define NF__SS_GPIO_Port GPIOB
#define ESP_TX_Pin GPIO_PIN_9
#define ESP_TX_GPIO_Port GPIOA
#define ESP_RX_Pin GPIO_PIN_10
#define ESP_RX_GPIO_Port GPIOA
#define AD_IRQ_Pin GPIO_PIN_3
#define AD_IRQ_GPIO_Port GPIOD
#define AD_IRQ_EXTI_IRQn EXTI2_3_IRQn
#define AD_CLK_Pin GPIO_PIN_3
#define AD_CLK_GPIO_Port GPIOB
#define AD_MISO_Pin GPIO_PIN_4
#define AD_MISO_GPIO_Port GPIOB
#define AD_MOSI_Pin GPIO_PIN_5
#define AD_MOSI_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
