#ifndef AT09_BLE_H
#define AT09_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Maximum sizes for TX/RX buffers */
#define AT09_RX_BUF_SIZE  128
#define AT09_TX_BUF_SIZE  128

/* Video frame streaming (128x64 monochrome = 1024 bytes) */
#define AT09_FRAME_SIZE       1024
#define AT09_DATA_TYPE_VIDEO  0x02

/* Debug globals — add to CubeIDE Live Expressions */
extern volatile uint8_t  dbg_uart_rx_buf[AT09_RX_BUF_SIZE];
extern volatile uint16_t dbg_uart_rx_len;
extern volatile uint8_t  dbg_uart_last_byte;

/* Video frame double-buffer: written by ISR, consumed by main loop */
extern volatile uint8_t  at09_frame_buf[AT09_FRAME_SIZE];
extern volatile uint8_t  at09_frame_ready;

typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t  rxBuf[AT09_RX_BUF_SIZE];
    uint16_t rxLen;
    volatile uint8_t rxReady;
} AT09_Handle_t;

/**
 * @brief  Initialise the AT-09 driver with the given UART handle.
 *         Starts interrupt-based single-byte reception.
 * @param  huart  Pointer to the HAL UART handle wired to the AT-09 module
 */
void AT09_Init(UART_HandleTypeDef *huart);

/**
 * @brief  Send a raw string over the BLE UART link (blocking).
 * @param  data  Null-terminated string to send
 * @return HAL status
 */
HAL_StatusTypeDef AT09_SendString(const char *data);

/**
 * @brief  Send raw bytes over the BLE UART link (blocking).
 * @param  data  Pointer to data buffer
 * @param  len   Number of bytes to send
 * @return HAL status
 */
HAL_StatusTypeDef AT09_SendBytes(const uint8_t *data, uint16_t len);

/**
 * @brief  Send an AT command and wait for a response.
 * @param  cmd       AT command string, e.g. "AT" or "AT+NAME?"
 * @param  resp      Buffer to store the response
 * @param  respSize  Size of the response buffer
 * @param  timeoutMs Timeout in milliseconds
 * @return true if a response was received before the timeout
 */
bool AT09_SendCommand(const char *cmd, char *resp, uint16_t respSize, uint32_t timeoutMs);

/**
 * @brief  Check if new data has been received via interrupt.
 * @return true if unread data is available
 */
bool AT09_DataAvailable(void);

/**
 * @brief  Read received data into the caller's buffer.
 *         Clears the internal receive buffer afterwards.
 * @param  buf     Destination buffer
 * @param  bufSize Size of the destination buffer
 * @return Number of bytes copied
 */
uint16_t AT09_Read(uint8_t *buf, uint16_t bufSize);

/**
 * @brief  Acknowledge that the video frame has been consumed.
 *         Call after copying at09_frame_buf to the display.
 */
void AT09_AckFrame(void);

/**
 * @brief  Check for partial-frame timeout.
 *         Call from the main loop. If a frame has been in progress
 *         for too long (~2 s), the state machine resets to idle.
 */
void AT09_CheckFrameTimeout(void);

/**
 * @brief  Call this from HAL_UART_RxCpltCallback for the AT-09 UART.
 *         It accumulates bytes and re-arms the single-byte IT reception.
 * @param  huart  The UART handle that triggered the callback
 */
void AT09_UART_RxCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* AT09_BLE_H */
