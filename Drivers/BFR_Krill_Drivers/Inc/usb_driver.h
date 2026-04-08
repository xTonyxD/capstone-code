#ifndef USB_DRIVER_H
#define USB_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void USB_Driver_Init(void);
bool USB_Driver_IsConfigured(void);

bool USB_Driver_Write(const uint8_t *data, uint16_t len);
bool USB_Driver_WriteString(const char *str);

void USB_Driver_RxHandler(const uint8_t *buf, uint32_t len);
void USB_Driver_SetConfigured(bool configured);

uint16_t USB_Driver_Available(void);
uint16_t USB_Driver_Read(uint8_t *dst, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif
