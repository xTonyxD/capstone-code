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
#include "dac.h"
#include "dma.h"
#include "i2c.h"
#include "icache.h"
#include "tsc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sh1106.h"
#include "at09_ble.h"
#include "audio_packet.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
  uint32_t raw;
  uint32_t baseline;
  uint32_t threshold;
  uint32_t ambient_threshold;
  uint32_t delta;
  uint32_t threshold_min;
  uint32_t threshold_divisor;
  uint32_t ambient_threshold_min;
  uint32_t ambient_threshold_divisor;
  uint8_t baseline_ok;
  uint8_t is_active;
  uint8_t last_tx_byte;
} TouchDebug_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define TOUCH_SAMPLE_INTERVAL_MS  5U
#define TOUCH_BASELINE_SAMPLES    8U
#define TOUCH_THRESHOLD_MIN       20U
#define TOUCH_THRESHOLD_DIVISOR   4U
#define TOUCH_AMBIENT_THRESHOLD_MIN      200U
#define TOUCH_AMBIENT_THRESHOLD_DIVISOR  2U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

static bool face_state_known = false;
static bool face_is_happy = false;
static bool touch_baseline_valid = false;
static bool touch_active = false;
static uint32_t touch_baseline = 0;
static uint32_t touch_last_sample_ms = 0;

volatile TouchDebug_t dbg_touch = {
  .raw = 0,
  .baseline = 0,
  .threshold = 0,
  .ambient_threshold = 0,
  .delta = 0,
  .threshold_min = TOUCH_THRESHOLD_MIN,
  .threshold_divisor = TOUCH_THRESHOLD_DIVISOR,
  .ambient_threshold_min = TOUCH_AMBIENT_THRESHOLD_MIN,
  .ambient_threshold_divisor = TOUCH_AMBIENT_THRESHOLD_DIVISOR,
  .baseline_ok = 0,
  .is_active = 0,
  .last_tx_byte = 0,
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void DrawFace(bool happy);
static void SetFaceState(bool happy);
static bool Touch_ReadRaw(uint32_t *value);
static void Touch_InitBaseline(void);
static void Touch_Process(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void DrawFace(bool happy) {
  SH1106_Fill(SH1106_COLOR_BLACK);

  // Head outline: circle centred at (64,32) r=28
  SH1106_DrawCircle(64, 32, 28, SH1106_COLOR_WHITE);

  // Left eye
  SH1106_DrawFilledCircle(54, 24, 3, SH1106_COLOR_WHITE);
  // Right eye
  SH1106_DrawFilledCircle(74, 24, 3, SH1106_COLOR_WHITE);

  if (happy) {
    // Smile: arc approximated with short line segments
    for (int i = -10; i < 10; i++) {
      int x = 64 + i;
      // parabola opening downward for smile
      int y = 40 + (i * i) / 12;
      SH1106_DrawPixel(x, y, SH1106_COLOR_WHITE);
      SH1106_DrawPixel(x, y + 1, SH1106_COLOR_WHITE);
    }
  } else {
    // Frown: arc approximated with short line segments
    for (int i = -10; i < 10; i++) {
      int x = 64 + i;
      // parabola opening upward for frown
      int y = 48 - (i * i) / 12;
      SH1106_DrawPixel(x, y, SH1106_COLOR_WHITE);
      SH1106_DrawPixel(x, y + 1, SH1106_COLOR_WHITE);
    }
  }

  SH1106_UpdateScreen();
}

static void SetFaceState(bool happy) {
  if (face_state_known && face_is_happy == happy) {
    return;
  }

  face_is_happy = happy;
  face_state_known = true;
  DrawFace(happy);

  {
    uint8_t negated_state = happy ? '0' : '1';
    AT09_SendBytes(&negated_state, 1);
  }
}

static bool Touch_ReadRaw(uint32_t *value) {
  if (HAL_TSC_Start(&htsc) != HAL_OK) {
    return false;
  }

  if (HAL_TSC_PollForAcquisition(&htsc) != HAL_OK) {
    return false;
  }

  *value = HAL_TSC_GroupGetValue(&htsc, TSC_GROUP1_IDX);
  dbg_touch.raw = *value;
  return true;
}

static void Touch_InitBaseline(void) {
  uint32_t sum = 0;
  uint32_t sample = 0;
  uint32_t sample_count = 0;

  HAL_Delay(20);

  for (uint32_t i = 0; i < TOUCH_BASELINE_SAMPLES; i++) {
    if (Touch_ReadRaw(&sample)) {
      sum += sample;
      sample_count++;
    }
    HAL_Delay(2);
  }

  if (sample_count > 0U) {
    touch_baseline = sum / sample_count;
    touch_baseline_valid = true;
    touch_last_sample_ms = HAL_GetTick();
    dbg_touch.baseline = touch_baseline;
    dbg_touch.baseline_ok = 1U;
  }
}

static void Touch_Process(void) {
  uint32_t sample = 0;
  uint32_t threshold = 0;
  uint32_t ambient_threshold = 0;
  uint32_t delta = 0;
  bool touched = false;

  if (!touch_baseline_valid) {
    dbg_touch.baseline_ok = 0U;
    return;
  }

  if ((HAL_GetTick() - touch_last_sample_ms) < TOUCH_SAMPLE_INTERVAL_MS) {
    return;
  }
  touch_last_sample_ms = HAL_GetTick();

  if (!Touch_ReadRaw(&sample)) {
    return;
  }

  threshold = touch_baseline / TOUCH_THRESHOLD_DIVISOR;
  if (threshold < TOUCH_THRESHOLD_MIN) {
    threshold = TOUCH_THRESHOLD_MIN;
  }

  ambient_threshold = threshold / TOUCH_AMBIENT_THRESHOLD_DIVISOR;
  if (ambient_threshold < TOUCH_AMBIENT_THRESHOLD_MIN) {
    ambient_threshold = TOUCH_AMBIENT_THRESHOLD_MIN;
  }

  dbg_touch.threshold = threshold;
  dbg_touch.ambient_threshold = ambient_threshold;

  if (sample <= touch_baseline) {
    delta = touch_baseline - sample;
  }
  dbg_touch.delta = delta;

  if (touch_active) {
    touched = (delta >= ambient_threshold);
  } else {
    touched = (delta >= threshold);
  }

  if (!touched && delta < ambient_threshold) {
    touch_baseline = ((touch_baseline * 15U) + sample) / 16U;
    dbg_touch.baseline = touch_baseline;
  }

  if (touched && !touch_active) {
    uint8_t touch_byte = 0x31;
    AT09_SendBytes(&touch_byte, 1);
    dbg_touch.last_tx_byte = touch_byte;
  }

  touch_active = touched;
  dbg_touch.is_active = touched ? 1U : 0U;
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
  MX_USART2_UART_Init();
  MX_ICACHE_Init();
  MX_DAC1_Init();
  MX_I2C2_Init();
  MX_USART1_UART_Init();
  MX_TSC_Init();
  /* USER CODE BEGIN 2 */

  /* I2C bus scan: sweep all 7-bit addresses and report over USART2 */
  {
    char msg[48];
    const char *hdr = "[I2C] Scanning bus...\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t *)hdr, strlen(hdr), 500);
    int found = 0;
    for (uint8_t addr = 1; addr <= 127; addr++) {
      if (HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(addr << 1), 2, 50) == HAL_OK) {
        int n = snprintf(msg, sizeof(msg), "[I2C] ACK at 0x%02X\r\n", addr);
        HAL_UART_Transmit(&huart2, (uint8_t *)msg, n, 500);
        found++;
      }
    }
    if (found == 0) {
      const char *none = "[I2C] No devices found\r\n";
      HAL_UART_Transmit(&huart2, (uint8_t *)none, strlen(none), 500);
    } else {
      int n = snprintf(msg, sizeof(msg), "[I2C] Scan done, %d device(s)\r\n", found);
      HAL_UART_Transmit(&huart2, (uint8_t *)msg, n, 500);
    }
  }

  SH1106_Init();
  AT09_Init(&huart1);
  Touch_InitBaseline();
  Audio_Init();

  // Show smiley by default on boot
  SetFaceState(true);

  // Pre-fill ring buffer with fake 440 Hz tone, then start DAC playback.
  // Comment out these two lines once real BLE audio packets are arriving.
  Audio_FeedFakeData();
  Audio_StartPlayback();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* ---- Video frame streaming (data type 0x02) ---- */
    AT09_CheckFrameTimeout();
    if (at09_frame_ready) {
      SH1106_SetBufferAndFlush((const uint8_t *)at09_frame_buf);
      AT09_AckFrame();
    }

    /* ---- Audio packet streaming (start byte 0xAA) ---- */
    if (at09_audio_pkt_ready) {
      Audio_ProcessPacket((const uint8_t *)at09_audio_pkt_buf);
      AT09_AckAudioPacket();
      if (!Audio_IsPlaying()) {
        Audio_StartPlayback();
      }
    }

    /* Keep the fake 440 Hz tone looping (remove once using real BLE data) */
    Audio_FeedFakeData();

    Touch_Process();

    /* ---- Legacy single-byte commands ---- */
    if (AT09_DataAvailable()) {
      uint8_t buf[AT09_RX_BUF_SIZE];
      uint16_t len = AT09_Read(buf, sizeof(buf));
      for (uint16_t i = 0; i < len; i++) {
        if (buf[i] == '1') {
          SetFaceState(true);
        } else if (buf[i] == '0') {
          SetFaceState(false);
        }
      }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
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
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  AT09_UART_RxCpltCallback(huart);


}
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
