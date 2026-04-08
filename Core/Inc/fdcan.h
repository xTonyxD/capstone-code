/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    fdcan.h
  * @brief   This file contains all the function prototypes for
  * the fdcan.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#ifndef __FDCAN_H__
#define __FDCAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
/* REMOVED: #include <fdcan.h> - This was causing a recursive loop! */
/* USER CODE END Includes */

extern FDCAN_HandleTypeDef hfdcan1;

/* USER CODE BEGIN Private defines */
/* Add any custom defines here */
/* USER CODE END Private defines */

void MX_FDCAN1_Init(void);

/* USER CODE BEGIN Prototypes */
/**
  * @brief  Initializes the FDCAN application logic and filters
  */
void FDCAN_App_Init(void);

/**
  * @brief  Sends an FDCAN message
  * @param  id: Standard or Extended Identifier
  * @param  data: Pointer to data buffer
  * @param  len: Length of data in bytes
  */
void FDCAN_Send_Message(uint32_t id, uint8_t *data, uint8_t len);

/**
  * @brief  Callback for receiving FDCAN messages (usually called from IRQ)
  */
void FDCAN_Receive_Callback(FDCAN_HandleTypeDef *hfdcan);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __FDCAN_H__ */

