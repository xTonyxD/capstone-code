#include "at09_ble.h"
#include <string.h>

/* ---- Private state ---- */
static AT09_Handle_t at09;
static uint8_t rxByte;  /* single-byte IT reception target */

/* ---- Debug: visible in Live Expressions ---- */
volatile uint8_t  dbg_uart_rx_buf[AT09_RX_BUF_SIZE]; /* raw RX bytes   */
volatile uint16_t dbg_uart_rx_len;                    /* current length  */
volatile uint8_t  dbg_uart_last_byte;                 /* last byte in    */

/* ---- Video frame accumulator ---- */
volatile uint8_t  at09_frame_buf[AT09_FRAME_SIZE];
volatile uint8_t  at09_frame_ready = 0;
static   uint16_t frame_pos = 0;
static   volatile uint32_t frame_start_tick = 0;

/* ---- Audio packet accumulator ---- */
volatile uint8_t  at09_audio_pkt_buf[AT09_AUDIO_PKT_SIZE];
volatile uint8_t  at09_audio_pkt_ready = 0;
static   uint16_t audio_pkt_pos = 0;
static   volatile uint32_t audio_pkt_start_tick = 0;

/* Timeout: if a frame isn't complete within this many ms, discard it */
#define AT09_FRAME_TIMEOUT_MS  2000
#define AT09_AUDIO_TIMEOUT_MS  500

typedef enum { RX_IDLE, RX_VIDEO_FRAME, RX_AUDIO_PACKET } AT09_RxState_t;
static volatile AT09_RxState_t rxState = RX_IDLE;

/* ---- Public API -------------------------------------------------------- */

void AT09_Init(UART_HandleTypeDef *huart)
{
    at09.huart   = huart;
    at09.rxLen   = 0;
    at09.rxReady = 0;
    memset(at09.rxBuf, 0, sizeof(at09.rxBuf));

    /* Start single-byte interrupt reception */
    HAL_UART_Receive_IT(at09.huart, &rxByte, 1);
}

HAL_StatusTypeDef AT09_SendString(const char *data)
{
    uint16_t len = (uint16_t)strlen(data);
    return HAL_UART_Transmit(at09.huart, (const uint8_t *)data, len, HAL_MAX_DELAY);
}

HAL_StatusTypeDef AT09_SendBytes(const uint8_t *data, uint16_t len)
{
    return HAL_UART_Transmit(at09.huart, data, len, HAL_MAX_DELAY);
}

bool AT09_SendCommand(const char *cmd, char *resp, uint16_t respSize, uint32_t timeoutMs)
{
    /* Clear any pending RX data */
    at09.rxLen   = 0;
    at09.rxReady = 0;

    /* Transmit the command (AT-09 does NOT need \r\n for most commands) */
    AT09_SendString(cmd);

    /* Wait for response */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeoutMs) {
        if (at09.rxReady) {
            uint16_t copyLen = at09.rxLen < (respSize - 1) ? at09.rxLen : (respSize - 1);
            memcpy(resp, at09.rxBuf, copyLen);
            resp[copyLen] = '\0';
            at09.rxLen   = 0;
            at09.rxReady = 0;
            return true;
        }
    }

    /* Timeout — copy whatever we got */
    if (at09.rxLen > 0) {
        uint16_t copyLen = at09.rxLen < (respSize - 1) ? at09.rxLen : (respSize - 1);
        memcpy(resp, at09.rxBuf, copyLen);
        resp[copyLen] = '\0';
        at09.rxLen   = 0;
        at09.rxReady = 0;
        return true;
    }
    resp[0] = '\0';
    return false;
}

bool AT09_DataAvailable(void)
{
    return (at09.rxReady != 0);
}

uint16_t AT09_Read(uint8_t *buf, uint16_t bufSize)
{
    if (!at09.rxReady && at09.rxLen == 0) return 0;

    uint16_t copyLen = at09.rxLen < bufSize ? at09.rxLen : bufSize;
    memcpy(buf, at09.rxBuf, copyLen);
    at09.rxLen   = 0;
    at09.rxReady = 0;
    return copyLen;
}

void AT09_AckFrame(void)
{
    at09_frame_ready = 0;
}

void AT09_AckAudioPacket(void)
{
    at09_audio_pkt_ready = 0;
}

void AT09_CheckFrameTimeout(void)
{
    if (rxState == RX_VIDEO_FRAME &&
        (HAL_GetTick() - frame_start_tick) > AT09_FRAME_TIMEOUT_MS) {
        rxState   = RX_IDLE;
        frame_pos = 0;
    }
    if (rxState == RX_AUDIO_PACKET &&
        (HAL_GetTick() - audio_pkt_start_tick) > AT09_AUDIO_TIMEOUT_MS) {
        rxState       = RX_IDLE;
        audio_pkt_pos = 0;
    }
}

/* ---- ISR callback ------------------------------------------------------ */

void AT09_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != at09.huart->Instance) return;

    switch (rxState) {
    case RX_VIDEO_FRAME:
        at09_frame_buf[frame_pos++] = rxByte;
        if (frame_pos >= AT09_FRAME_SIZE) {
            at09_frame_ready = 1;
            rxState = RX_IDLE;
        }
        break;

    case RX_AUDIO_PACKET:
        at09_audio_pkt_buf[audio_pkt_pos++] = rxByte;
        if (audio_pkt_pos >= AT09_AUDIO_PKT_SIZE) {
            at09_audio_pkt_ready = 1;
            rxState = RX_IDLE;
        }
        break;

    case RX_IDLE:
    default:
        if (rxByte == AT09_DATA_TYPE_VIDEO) {
            frame_pos = 0;
            frame_start_tick = HAL_GetTick();
            rxState = RX_VIDEO_FRAME;
        } else if (rxByte == AT09_DATA_TYPE_AUDIO && !at09_audio_pkt_ready) {
            at09_audio_pkt_buf[0] = rxByte;
            audio_pkt_pos = 1;
            audio_pkt_start_tick = HAL_GetTick();
            rxState = RX_AUDIO_PACKET;
        } else {
            /* Normal data — pass to AT09 command buffer */
            if (at09.rxLen < AT09_RX_BUF_SIZE - 1) {
                at09.rxBuf[at09.rxLen++] = rxByte;
            }
            dbg_uart_last_byte = rxByte;
            memcpy((void *)dbg_uart_rx_buf, at09.rxBuf, at09.rxLen);
            dbg_uart_rx_len = at09.rxLen;
            at09.rxReady = 1;
        }
        break;
    }

    /* Re-arm single-byte IT reception */
    HAL_UART_Receive_IT(at09.huart, &rxByte, 1);
}
