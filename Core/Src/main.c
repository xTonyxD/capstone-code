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

typedef enum {
  BT_PROTO_WAIT_SYNC0 = 0,
  BT_PROTO_WAIT_SYNC1,
  BT_PROTO_WAIT_TYPE,
  BT_PROTO_WAIT_LENGTH,
  BT_PROTO_WAIT_PAYLOAD,
  BT_PROTO_WAIT_CHECKSUM,
} BT_ProtocolParseState_t;

typedef struct {
  BT_ProtocolParseState_t state;
  uint8_t type;
  uint8_t length;
  uint8_t index;
  uint8_t payload[16];
} BT_ProtocolParser_t;

typedef struct {
  uint32_t baud_rate;
  const char *at_command;
} BT05_BaudSetting_t;

typedef enum {
  FACE_HAPPY       = 0,
  FACE_DISTRACTED  = 1,
  FACE_DISTURBED   = 2,
  FACE_CRISIS      = 3,
} FaceState_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define TOUCH_SAMPLE_INTERVAL_MS  5U
#define TOUCH_BASELINE_SAMPLES    8U
#define TOUCH_THRESHOLD_MIN       20U
#define TOUCH_THRESHOLD_DIVISOR   11U
#define TOUCH_AMBIENT_THRESHOLD_MIN      200U
#define TOUCH_AMBIENT_THRESHOLD_DIVISOR  2U

#define BT_PROTO_SYNC0                  0xA5U
#define BT_PROTO_SYNC1                  0x5AU
#define BT_PROTO_MAX_PAYLOAD            16U
#define BT_PROTO_CMD_SET_FACE           0x01U
#define BT_PROTO_CMD_GET_STATUS         0x02U
#define BT_PROTO_CMD_PING               0x03U
#define BT_PROTO_CMD_SET_BT05_BAUD      0x04U
#define BT_PROTO_EVT_TOUCH_STATE        0x81U
#define BT_PROTO_EVT_FACE_STATE         0x82U
#define BT_PROTO_EVT_STATUS             0x83U
#define BT_PROTO_EVT_PONG               0x84U
#define BT_PROTO_EVT_BT05_BAUD_RESULT   0x85U

#define BT05_BAUD_STATUS_OK             0U
#define BT05_BAUD_STATUS_UNSUPPORTED    1U
#define BT05_BAUD_STATUS_LINK_DOWN      2U
#define BT05_BAUD_STATUS_COMMAND_FAIL   3U
#define BT05_BAUD_STATUS_UART_REINIT    4U
#define BT05_BAUD_STATUS_VERIFY_FAIL    5U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

static bool face_state_known = false;
static FaceState_t face_state = FACE_HAPPY;
static bool touch_baseline_valid = false;
static bool touch_active = false;
static bool fake_audio_enabled = true;
static uint32_t touch_baseline = 0;
static uint32_t touch_last_sample_ms = 0;
static BT_ProtocolParser_t bt_protocol_parser = {0};
static uint32_t blink_last_ms = 0;
static bool blink_is_closed = false;

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
static void DrawFace(FaceState_t state);
static void DrawCuteEye(int16_t center_x, int16_t center_y, FaceState_t state, bool left_eye);
static void SetFaceState(FaceState_t state);
static bool Touch_ReadRaw(uint32_t *value);
static void Touch_InitBaseline(void);
static void Touch_Process(void);
static void BT_ProtocolResetParser(void);
static bool BT_ProtocolProcessByte(uint8_t byte);
static void BT_ProtocolHandleFrame(uint8_t type, const uint8_t *payload, uint8_t length);
static void BT_ProtocolSendFrame(uint8_t type, const uint8_t *payload, uint8_t length);
static void BT_ProtocolSendFaceState(FaceState_t state);
static void BT_ProtocolSendTouchState(bool active);
static void BT_ProtocolSendStatus(void);
static void BT_ProtocolSendPong(void);
static void BT_ProtocolSendBt05BaudResult(uint8_t status, uint32_t baud_rate);
static uint32_t BT_ProtocolReadU32LE(const uint8_t *src);
static const BT05_BaudSetting_t *BT05_FindBaudSetting(uint32_t baud_rate);
static bool BT05_ReconfigureLocalUart(uint32_t baud_rate);
static uint8_t BT05_ConfigureBaud(uint32_t baud_rate);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void DrawCuteEye(int16_t center_x, int16_t center_y, FaceState_t state, bool left_eye) {
  const int16_t eye_scale = 3;
  const int16_t eye_radius = 6 * eye_scale;
  const int16_t pupil_radius = 2 * eye_scale;
  const int16_t highlight_radius = eye_scale - 1;
  int16_t pupil_x, pupil_y;

  SH1106_DrawFilledCircle(center_x, center_y, eye_radius, SH1106_COLOR_WHITE);

  switch (state) {
    case FACE_HAPPY:
    default:
      /* Symmetric trim at top, pupils up-right */
      pupil_x = center_x + eye_scale;
      pupil_y = center_y + eye_scale;
      SH1106_DrawLine(center_x - (5 * eye_scale), center_y - (8 * eye_scale),
                      center_x - eye_scale,        center_y - (6 * eye_scale), SH1106_COLOR_WHITE);
      SH1106_DrawLine(center_x + eye_scale,        center_y - (6 * eye_scale),
                      center_x + (5 * eye_scale),  center_y - (8 * eye_scale), SH1106_COLOR_WHITE);
      break;

    case FACE_DISTRACTED:
      /* Both pupils drift right (zoning out), left brow raised higher */
      pupil_x = center_x + (2 * eye_scale);
      pupil_y = center_y;
      if (left_eye) {
        SH1106_DrawLine(center_x - (5 * eye_scale), center_y - (9 * eye_scale),
                        center_x + (3 * eye_scale), center_y - (7 * eye_scale), SH1106_COLOR_WHITE);
      } else {
        SH1106_DrawLine(center_x - (5 * eye_scale), center_y - (8 * eye_scale),
                        center_x - eye_scale,        center_y - (6 * eye_scale), SH1106_COLOR_WHITE);
        SH1106_DrawLine(center_x + eye_scale,        center_y - (6 * eye_scale),
                        center_x + (5 * eye_scale),  center_y - (8 * eye_scale), SH1106_COLOR_WHITE);
      }
      break;

    case FACE_DISTURBED:
      /* Brows angled inward (furrowed), pupils downcast toward center */
      pupil_x = left_eye ? (center_x - eye_scale) : (center_x + eye_scale);
      pupil_y = center_y + (2 * eye_scale);
      if (left_eye) {
        SH1106_DrawLine(center_x - (6 * eye_scale), center_y - (4 * eye_scale),
                        center_x + (6 * eye_scale), center_y - eye_scale,    SH1106_COLOR_BLACK);
        SH1106_DrawLine(center_x - (6 * eye_scale), center_y - (3 * eye_scale),
                        center_x + (6 * eye_scale), center_y,                SH1106_COLOR_BLACK);
        SH1106_DrawLine(center_x - (5 * eye_scale), center_y - (9 * eye_scale),
                        center_x + (4 * eye_scale), center_y - (7 * eye_scale), SH1106_COLOR_WHITE);
      } else {
        SH1106_DrawLine(center_x - (6 * eye_scale), center_y - eye_scale,
                        center_x + (6 * eye_scale), center_y - (4 * eye_scale), SH1106_COLOR_BLACK);
        SH1106_DrawLine(center_x - (6 * eye_scale), center_y,
                        center_x + (6 * eye_scale), center_y - (3 * eye_scale), SH1106_COLOR_BLACK);
        SH1106_DrawLine(center_x - (4 * eye_scale), center_y - (7 * eye_scale),
                        center_x + (5 * eye_scale), center_y - (9 * eye_scale), SH1106_COLOR_WHITE);
      }
      break;

    case FACE_CRISIS:
      /* Wide-open eyes (no eyelid trim), pupils small and centered — shocked look */
      pupil_x = center_x;
      pupil_y = center_y;
      break;
  }

  SH1106_DrawFilledCircle(pupil_x, pupil_y, pupil_radius, SH1106_COLOR_BLACK);
  SH1106_DrawFilledCircle(pupil_x - eye_scale, pupil_y - eye_scale, highlight_radius, SH1106_COLOR_WHITE);
}

static void DrawFace(FaceState_t state) {
  SH1106_Fill(SH1106_COLOR_BLACK);

  int16_t eye_y = (state == FACE_DISTURBED) ? 33 : 30;
  DrawCuteEye(42, eye_y, state, true);
  DrawCuteEye(86, eye_y, state, false);

  SH1106_UpdateScreen();
}

static uint8_t BT_ProtocolChecksum(uint8_t type, uint8_t length, const uint8_t *payload) {
  uint8_t checksum = type ^ length;

  for (uint8_t i = 0; i < length; i++) {
    checksum ^= payload[i];
  }

  return checksum;
}

static uint16_t BT_ProtocolClampU16(uint32_t value) {
  if (value > 0xFFFFU) {
    return 0xFFFFU;
  }

  return (uint16_t)value;
}

static void BT_ProtocolWriteU16LE(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void BT_ProtocolWriteU32LE(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16) & 0xFFU);
  dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t BT_ProtocolReadU32LE(const uint8_t *src) {
  return ((uint32_t)src[0]) |
         ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static const BT05_BaudSetting_t *BT05_FindBaudSetting(uint32_t baud_rate) {
  static const BT05_BaudSetting_t settings[] = {
    { 9600U,   "AT+BAUD0" },
    { 19200U,  "AT+BAUD1" },
    { 38400U,  "AT+BAUD2" },
    { 57600U,  "AT+BAUD3" },
    { 115200U, "AT+BAUD4" },
    { 230400U, "AT+BAUD8" },
  };

  for (uint32_t i = 0; i < (sizeof(settings) / sizeof(settings[0])); i++) {
    if (settings[i].baud_rate == baud_rate) {
      return &settings[i];
    }
  }

  return NULL;
}

static bool BT05_ReconfigureLocalUart(uint32_t baud_rate) {
  if (HAL_UART_Abort(&huart1) != HAL_OK) {
    return false;
  }

  if (HAL_UART_DeInit(&huart1) != HAL_OK) {
    return false;
  }

  huart1.Init.BaudRate = baud_rate;

  if (HAL_UART_Init(&huart1) != HAL_OK) {
    return false;
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
    return false;
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
    return false;
  }

  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK) {
    return false;
  }

  AT09_Init(&huart1);
  return true;
}

static uint8_t BT05_ConfigureBaud(uint32_t baud_rate) {
  char response[32];
  uint32_t previous_baud = huart1.Init.BaudRate;
  const BT05_BaudSetting_t *setting = BT05_FindBaudSetting(baud_rate);

  if (setting == NULL) {
    return BT05_BAUD_STATUS_UNSUPPORTED;
  }

  if (!AT09_SendCommand("AT", response, sizeof(response), 250U)) {
    return BT05_BAUD_STATUS_LINK_DOWN;
  }

  if (!AT09_SendCommand(setting->at_command, response, sizeof(response), 300U)) {
    return BT05_BAUD_STATUS_COMMAND_FAIL;
  }

  HAL_Delay(60U);

  if (!BT05_ReconfigureLocalUart(baud_rate)) {
    BT05_ReconfigureLocalUart(previous_baud);
    return BT05_BAUD_STATUS_UART_REINIT;
  }

  for (uint32_t attempt = 0; attempt < 3U; attempt++) {
    HAL_Delay(40U);
    if (AT09_SendCommand("AT", response, sizeof(response), 250U)) {
      return BT05_BAUD_STATUS_OK;
    }
  }

  if (BT05_ReconfigureLocalUart(previous_baud)) {
    if (AT09_SendCommand("AT", response, sizeof(response), 250U)) {
      return BT05_BAUD_STATUS_VERIFY_FAIL;
    }
  }

  return BT05_BAUD_STATUS_VERIFY_FAIL;
}

static void BT_ProtocolResetParser(void) {
  bt_protocol_parser.state = BT_PROTO_WAIT_SYNC0;
  bt_protocol_parser.type = 0U;
  bt_protocol_parser.length = 0U;
  bt_protocol_parser.index = 0U;
}

static void BT_ProtocolSendFrame(uint8_t type, const uint8_t *payload, uint8_t length) {
  uint8_t frame[2U + 1U + 1U + BT_PROTO_MAX_PAYLOAD + 1U];

  if (length > BT_PROTO_MAX_PAYLOAD) {
    return;
  }

  frame[0] = BT_PROTO_SYNC0;
  frame[1] = BT_PROTO_SYNC1;
  frame[2] = type;
  frame[3] = length;

  if ((payload != NULL) && (length > 0U)) {
    memcpy(&frame[4], payload, length);
  }

  frame[4U + length] = BT_ProtocolChecksum(type, length, payload);
  AT09_SendBytes(frame, (uint16_t)(5U + length));
}

static void BT_ProtocolSendFaceState(FaceState_t state) {
  uint8_t payload[1];

  payload[0] = (uint8_t)state;
  BT_ProtocolSendFrame(BT_PROTO_EVT_FACE_STATE, payload, sizeof(payload));
}

static void BT_ProtocolSendTouchState(bool active) {
  uint8_t payload[5];

  payload[0] = active ? 1U : 0U;
  BT_ProtocolWriteU16LE(&payload[1], BT_ProtocolClampU16(dbg_touch.delta));
  BT_ProtocolWriteU16LE(&payload[3], BT_ProtocolClampU16(dbg_touch.raw));
  BT_ProtocolSendFrame(BT_PROTO_EVT_TOUCH_STATE, payload, sizeof(payload));
}

static void BT_ProtocolSendStatus(void) {
  uint8_t payload[11];

  payload[0] = (uint8_t)(((uint8_t)face_state & 0x03U) |
                         (touch_active ? 0x04U : 0x00U) |
                         (touch_baseline_valid ? 0x08U : 0x00U));
  BT_ProtocolWriteU16LE(&payload[1], BT_ProtocolClampU16(dbg_touch.raw));
  BT_ProtocolWriteU16LE(&payload[3], BT_ProtocolClampU16(dbg_touch.baseline));
  BT_ProtocolWriteU16LE(&payload[5], BT_ProtocolClampU16(dbg_touch.delta));
  BT_ProtocolWriteU16LE(&payload[7], BT_ProtocolClampU16(dbg_touch.threshold));
  BT_ProtocolWriteU16LE(&payload[9], BT_ProtocolClampU16(dbg_touch.ambient_threshold));
  BT_ProtocolSendFrame(BT_PROTO_EVT_STATUS, payload, sizeof(payload));
}

static void BT_ProtocolSendPong(void) {
  uint8_t payload[4];

  BT_ProtocolWriteU32LE(payload, HAL_GetTick());
  BT_ProtocolSendFrame(BT_PROTO_EVT_PONG, payload, sizeof(payload));
}

static void BT_ProtocolSendBt05BaudResult(uint8_t status, uint32_t baud_rate) {
  uint8_t payload[5];

  payload[0] = status;
  BT_ProtocolWriteU32LE(&payload[1], baud_rate);
  BT_ProtocolSendFrame(BT_PROTO_EVT_BT05_BAUD_RESULT, payload, sizeof(payload));
}

static void BT_ProtocolHandleFrame(uint8_t type, const uint8_t *payload, uint8_t length) {
  switch (type) {
    case BT_PROTO_CMD_SET_FACE:
      if (length >= 1U && payload[0] <= 3U) {
        static const FaceState_t face_map[4] = {
          FACE_CRISIS, FACE_DISTRACTED, FACE_HAPPY, FACE_DISTURBED
        };
        SetFaceState(face_map[payload[0]]);
      }
      break;

    case BT_PROTO_CMD_GET_STATUS:
      BT_ProtocolSendStatus();
      break;

    case BT_PROTO_CMD_PING:
      BT_ProtocolSendPong();
      break;

    case BT_PROTO_CMD_SET_BT05_BAUD:
      if (length >= 4U) {
        uint32_t requested_baud = BT_ProtocolReadU32LE(payload);
        uint8_t result = BT05_ConfigureBaud(requested_baud);
        BT_ProtocolSendBt05BaudResult(result, requested_baud);
      }
      break;

    default:
      break;
  }
}

static bool BT_ProtocolProcessByte(uint8_t byte) {
  switch (bt_protocol_parser.state) {
    case BT_PROTO_WAIT_SYNC0:
      if (byte == BT_PROTO_SYNC0) {
        bt_protocol_parser.state = BT_PROTO_WAIT_SYNC1;
        return true;
      }
      return false;

    case BT_PROTO_WAIT_SYNC1:
      if (byte == BT_PROTO_SYNC1) {
        bt_protocol_parser.state = BT_PROTO_WAIT_TYPE;
      } else if (byte == BT_PROTO_SYNC0) {
        bt_protocol_parser.state = BT_PROTO_WAIT_SYNC1;
      } else {
        BT_ProtocolResetParser();
      }
      return true;

    case BT_PROTO_WAIT_TYPE:
      bt_protocol_parser.type = byte;
      bt_protocol_parser.state = BT_PROTO_WAIT_LENGTH;
      return true;

    case BT_PROTO_WAIT_LENGTH:
      if (byte > BT_PROTO_MAX_PAYLOAD) {
        BT_ProtocolResetParser();
        return true;
      }

      bt_protocol_parser.length = byte;
      bt_protocol_parser.index = 0U;
      bt_protocol_parser.state = (byte == 0U) ? BT_PROTO_WAIT_CHECKSUM : BT_PROTO_WAIT_PAYLOAD;
      return true;

    case BT_PROTO_WAIT_PAYLOAD:
      bt_protocol_parser.payload[bt_protocol_parser.index++] = byte;
      if (bt_protocol_parser.index >= bt_protocol_parser.length) {
        bt_protocol_parser.state = BT_PROTO_WAIT_CHECKSUM;
      }
      return true;

    case BT_PROTO_WAIT_CHECKSUM:
      if (byte == BT_ProtocolChecksum(bt_protocol_parser.type,
                                      bt_protocol_parser.length,
                                      bt_protocol_parser.payload)) {
        BT_ProtocolHandleFrame(bt_protocol_parser.type,
                               bt_protocol_parser.payload,
                               bt_protocol_parser.length);
      }
      BT_ProtocolResetParser();
      return true;

    default:
      BT_ProtocolResetParser();
      return false;
  }
}

static void DrawBlinkEyes(void) {
  SH1106_Fill(SH1106_COLOR_BLACK);
  SH1106_DrawFilledRectangle(42 - 15, 30 - 2, 30, 5, SH1106_COLOR_WHITE);
  SH1106_DrawFilledRectangle(86 - 15, 30 - 2, 30, 5, SH1106_COLOR_WHITE);
  SH1106_UpdateScreen();
}

static void Blink_Process(void) {
  if (face_state != FACE_CRISIS) return;

  uint32_t now = HAL_GetTick();

  if (!blink_is_closed) {
    if (now - blink_last_ms >= 3500U) {
      blink_is_closed = true;
      blink_last_ms = now;
      DrawBlinkEyes();
    }
  } else {
    if (now - blink_last_ms >= 400U) {
      blink_is_closed = false;
      blink_last_ms = now;
      DrawFace(FACE_CRISIS);
    }
  }
}

static void SetFaceState(FaceState_t state) {
  if (face_state_known && face_state == state) {
    return;
  }

  blink_is_closed = false;
  blink_last_ms = HAL_GetTick();

  face_state = state;
  face_state_known = true;
  DrawFace(state);

  {
    uint8_t echo = (uint8_t)('0' + (uint8_t)state);
    AT09_SendBytes(&echo, 1);
  }

  BT_ProtocolSendFaceState(state);
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

  if (touched != touch_active) {
    BT_ProtocolSendTouchState(touched);
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
  SetFaceState(FACE_CRISIS);

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
      if (fake_audio_enabled) {
        Audio_StopPlayback();
        Audio_Init();
        fake_audio_enabled = false;
      }

      Audio_ProcessPacket((const uint8_t *)at09_audio_pkt_buf);
      AT09_AckAudioPacket();
      if (!Audio_IsPlaying()) {
        Audio_StartPlayback();
      }
    }

    /* Keep either the fake tone or the last real packet feeding playback. */
    if (fake_audio_enabled) {
      Audio_FeedFakeData();
    } else {
      Audio_FeedLastPacket();
    }

    Touch_Process();
    Blink_Process();

    /* ---- Legacy single-byte commands ---- */
    if (AT09_DataAvailable()) {
      uint8_t buf[AT09_RX_BUF_SIZE];
      uint16_t len = AT09_Read(buf, sizeof(buf));
      for (uint16_t i = 0; i < len; i++) {
        if (BT_ProtocolProcessByte(buf[i])) {
          continue;
        }

        if      (buf[i] == '0') { SetFaceState(FACE_CRISIS); }
        else if (buf[i] == '1') { SetFaceState(FACE_DISTRACTED); }
        else if (buf[i] == '2') { SetFaceState(FACE_HAPPY); }
        else if (buf[i] == '3') { SetFaceState(FACE_DISTURBED); }
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
