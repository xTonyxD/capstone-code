/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "fdcan.h"
#include "i2c.h"
#include "icache.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sh1106.h"
#include "usb_driver.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_VREF            3.3f
#define ADC_MAX             4096.0f

#define OPAMP_GAIN          0.833f
#define SENSOR_OFFSET_V     0.5f
#define SENSOR_SENSITIVITY  0.100f // Change this (based on the datasheet)

#define NUM_SAMPLES         8
#define CAN_TX_INTERVAL_MS  100
#define DISPLAY_UPDATE_MS   200
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile int time;
volatile float voltage;
volatile float current;
volatile uint16_t AD_RES[NUM_SAMPLES];
volatile uint8_t adc_ready = 0;

FDCAN_TxHeaderTypeDef TxHeader;
uint8_t TxData[4];
uint32_t last_can_tx_time = 0;
uint32_t last_display_time = 0;
uint32_t last_usb_tx_time = 0;

typedef struct {
  uint32_t sensor_ns;
  uint32_t dma_ns;
  uint32_t oled_ns;
} perfstats;

perfstats my_perfstats;
uint32_t startTime = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Return raw ADC voltage (0-3.3)
float ReadADC(void) {
  uint32_t sum = 0;
	for (int i = 0; i < NUM_SAMPLES; i++) {
		sum += AD_RES[i];
	}
	return ((float)sum / NUM_SAMPLES * ADC_VREF) / ADC_MAX;
}

// Undo gain and calculate current from sensitivity
void SetCurrent(float adc_voltage) {
  float sensor_voltage = adc_voltage / OPAMP_GAIN;
  current = (sensor_voltage - SENSOR_OFFSET_V) / SENSOR_SENSITIVITY;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  if (hadc->Instance == ADC1) {
    adc_ready = 1;
    my_perfstats.dma_ns = HAL_GetTick() - startTime;
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_FDCAN1_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_ICACHE_Init();
  MX_USB_Device_Init();
  /* USER CODE BEGIN 2 */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)AD_RES, NUM_SAMPLES);

  HAL_FDCAN_Start(&hfdcan1);
  TxHeader.Identifier = 0x6A; // Probably gotta maybe perhaps change
  TxHeader.IdType = FDCAN_STANDARD_ID;
  TxHeader.TxFrameType = FDCAN_DATA_FRAME;
  TxHeader.DataLength = FDCAN_DLC_BYTES_4;
  TxHeader.FDFormat = FDCAN_CLASSIC_CAN;

  SH1106_Init();
  SH1106_GotoXY(2, 0);
  SH1106_Puts("Initializing...", &Font_7x10, 1);
  SH1106_DrawBitmap(2, 15, bfr_logo, 64, 64, 1);
  SH1106_UpdateScreenDMA();
  SH1106_Flush();
  HAL_Delay(1000);
  SH1106_Clear();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (adc_ready) {
      adc_ready = 0;
      startTime = HAL_GetTick();
      voltage = ReadADC();
      SetCurrent(voltage);
      uint32_t now = HAL_GetTick();

      if ((now - last_display_time) >= DISPLAY_UPDATE_MS) {
          last_display_time = now;
          SH1106_Flush();  // ensure previous DMA transfer is done
          SH1106_Fill(SH1106_COLOR_BLACK);  // clear framebuffer
          char buffer[22];
          snprintf(buffer, sizeof(buffer), "Current: %.2f A", current);
          SH1106_GotoXY(2, 0);
          SH1106_Puts(buffer, &Font_7x10, 1);
          SH1106_UpdateScreenDMA();  // non-blocking
      }

      if ((now - last_can_tx_time) >= CAN_TX_INTERVAL_MS) {
        last_can_tx_time = now;
        float current_snapshot = current;
        memcpy(TxData, &current_snapshot, sizeof(current_snapshot));
        HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, TxData);
      }

      if (USB_Driver_IsConfigured() && ((now - last_usb_tx_time) >= 1000U)) {
        last_usb_tx_time = now;

        char usb_buffer[64];
        snprintf(usb_buffer, sizeof(usb_buffer),
                 "ADC: %.3f V  Current: %.2f A\r\n",
                 voltage, current);
        USB_Driver_WriteString(usb_buffer);
      }

      time += 1;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 55;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
