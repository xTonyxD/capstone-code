#include "usb_driver.h"

#include <string.h>
#include "usbd_cdc_if.h"

#define USB_RX_BUFFER_SIZE 128U

static volatile bool s_usb_configured = false;
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static uint8_t s_rx_buffer[USB_RX_BUFFER_SIZE];

static uint16_t USB_Driver_NextIndex(uint16_t idx)
{
    return (uint16_t)((idx + 1U) % USB_RX_BUFFER_SIZE);
}

void USB_Driver_Init(void)
{
    s_usb_configured = false;
    s_rx_head = 0;
    s_rx_tail = 0;
    memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
}

void USB_Driver_SetConfigured(bool configured)
{
    s_usb_configured = configured;
}

bool USB_Driver_IsConfigured(void)
{
    return s_usb_configured;
}

bool USB_Driver_Write(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    if (!s_usb_configured)
    {
        return false;
    }

    return (CDC_Transmit_FS((uint8_t *)data, len) == USBD_OK);
}

bool USB_Driver_WriteString(const char *str)
{
    uint16_t len = 0U;

    if (str == NULL)
    {
        return false;
    }

    while (str[len] != '\0')
    {
        len++;
    }

    return USB_Driver_Write((const uint8_t *)str, len);
}

void USB_Driver_RxHandler(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U))
    {
        return;
    }

    for (uint32_t i = 0; i < len; i++)
    {
        uint16_t next = USB_Driver_NextIndex(s_rx_head);

        if (next == s_rx_tail)
        {
            break;
        }

        s_rx_buffer[s_rx_head] = buf[i];
        s_rx_head = next;
    }
}

uint16_t USB_Driver_Available(void)
{
    if (s_rx_head >= s_rx_tail)
    {
        return (uint16_t)(s_rx_head - s_rx_tail);
    }

    return (uint16_t)(USB_RX_BUFFER_SIZE - s_rx_tail + s_rx_head);
}

uint16_t USB_Driver_Read(uint8_t *dst, uint16_t max_len)
{
    uint16_t count = 0U;

    if ((dst == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    while ((count < max_len) && (s_rx_tail != s_rx_head))
    {
        dst[count++] = s_rx_buffer[s_rx_tail];
        s_rx_tail = USB_Driver_NextIndex(s_rx_tail);
    }

    return count;
}
