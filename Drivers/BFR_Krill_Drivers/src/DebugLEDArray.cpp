/*
 * DebugLEDArray.cpp
 *
 *  Created on: Mar 5, 2026
 *      Author: antho
 *
 * ok, so the goal of this driver...
 *
 * 1) initialize a debugLEDArray object
 * 2) not think about it, just whenever u wanna change a LED u just write a new bit to a uint_8t
 *    that stores the status of every LED. 1=on, 0=off
 * 3) decode that to state of 3 pins
 * 4) write to the pins whenever that gets updated
 *
 * must do this with minimal footprint (memory and compute)
 */

#include "DebugLEDArray.h"

DebugLEDArray::DebugLEDArray(
		GPIO_TypeDef *p0, uint16_t pin0,
        GPIO_TypeDef *p1, uint16_t pin1,
        GPIO_TypeDef *p2, uint16_t pin2) {
	states = 0;
	handle.port[0] = p0;
	handle.pin[0]  = pin0;

	handle.port[1] = p1;
	handle.pin[1]  = pin1;

	handle.port[2] = p2;
	handle.pin[2]  = pin2;


}

DebugLEDArray::~DebugLEDArray() {
	// TODO Auto-generated destructor stub
}

/**
  * @brief  Initializes the Mux handle with specific GPIOs
  * @param  mux: Pointer to the Mux handle
  * @param  p0-p2: GPIO Ports for Select bits S0, S1, S2
  * @param  pin0-pin2: GPIO Pins for Select bits S0, S1, S2
  */
void initDebugLEDArray(DebugLEDArray* arr)
{
    // Optional: Ensure all pins start LOW (Channel 0)
    for(int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(arr->handle.port[i], arr->handle.pin[i], GPIO_PIN_RESET);
    }
}


void DebugLEDArray::toggleOne(uint8_t index) {
	if (channel > 7) return;

	// Bit 0 (LSB) -> Controls S0
	GPIO_PinState s0 = (channel & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;

	// Bit 1 -> Controls S1 (Shift right by 1, then mask)
	GPIO_PinState s1 = ((channel >> 1) & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;

	// Bit 2 (MSB) -> Controls S2 (Shift right by 2, then mask)
	GPIO_PinState s2 = ((channel >> 2) & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;

	// Apply to your hardware pins
	HAL_GPIO_WritePin(handle.port[0], handle.pin[0], s0);
	HAL_GPIO_WritePin(handle.port[1], handle.pin[1], s1);
	HAL_GPIO_WritePin(handle.port[2], handle.pin[2], s2);
}
