/*
 * DebugLEDArray.h
 *
 *  Created on: Mar 5, 2026
 *      Author: antho
 */

#ifndef BFR_KRILL_DRIVERS_SRC_DEBUGLEDARRAY_H_
#define BFR_KRILL_DRIVERS_SRC_DEBUGLEDARRAY_H_
#include "stm32l5xx_hal.h"

typedef struct {
    GPIO_TypeDef* port[3]; // Array of ports
    uint16_t      pin[3];  // Array of pins
} Mux_Handle;




class DebugLEDArray {
public:
	DebugLEDArray();
	virtual ~DebugLEDArray();
	uint8_t states;
	void initDebugLEDArray(GPIO_TypeDef *p0, uint16_t pin0,
            GPIO_TypeDef *p1, uint16_t pin1,
            GPIO_TypeDef *p2, uint16_t pin2);


	void toggleOne(uint8_t index);
private:
	Mux_Handle handle;
};

#endif /* BFR_KRILL_DRIVERS_SRC_DEBUGLEDARRAY_H_ */
