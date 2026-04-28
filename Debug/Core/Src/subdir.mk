################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/at09_ble.c \
../Core/Src/audio_packet.c \
../Core/Src/dac.c \
../Core/Src/dma.c \
../Core/Src/gpio.c \
../Core/Src/i2c.c \
../Core/Src/icache.c \
../Core/Src/main.c \
../Core/Src/sh1106.c \
../Core/Src/stm32l5xx_hal_msp.c \
../Core/Src/stm32l5xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32l5xx.c \
../Core/Src/usart.c 

OBJS += \
./Core/Src/at09_ble.o \
./Core/Src/audio_packet.o \
./Core/Src/dac.o \
./Core/Src/dma.o \
./Core/Src/gpio.o \
./Core/Src/i2c.o \
./Core/Src/icache.o \
./Core/Src/main.o \
./Core/Src/sh1106.o \
./Core/Src/stm32l5xx_hal_msp.o \
./Core/Src/stm32l5xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32l5xx.o \
./Core/Src/usart.o 

C_DEPS += \
./Core/Src/at09_ble.d \
./Core/Src/audio_packet.d \
./Core/Src/dac.d \
./Core/Src/dma.d \
./Core/Src/gpio.d \
./Core/Src/i2c.d \
./Core/Src/icache.d \
./Core/Src/main.d \
./Core/Src/sh1106.d \
./Core/Src/stm32l5xx_hal_msp.d \
./Core/Src/stm32l5xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32l5xx.d \
./Core/Src/usart.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L552xx -c -I../Core/Inc -I../Drivers/BFR_Krill_Drivers/Inc -I../Drivers/STM32L5xx_HAL_Driver/Inc -I../Drivers/STM32L5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L5xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/at09_ble.cyclo ./Core/Src/at09_ble.d ./Core/Src/at09_ble.o ./Core/Src/at09_ble.su ./Core/Src/audio_packet.cyclo ./Core/Src/audio_packet.d ./Core/Src/audio_packet.o ./Core/Src/audio_packet.su ./Core/Src/dac.cyclo ./Core/Src/dac.d ./Core/Src/dac.o ./Core/Src/dac.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/i2c.cyclo ./Core/Src/i2c.d ./Core/Src/i2c.o ./Core/Src/i2c.su ./Core/Src/icache.cyclo ./Core/Src/icache.d ./Core/Src/icache.o ./Core/Src/icache.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/sh1106.cyclo ./Core/Src/sh1106.d ./Core/Src/sh1106.o ./Core/Src/sh1106.su ./Core/Src/stm32l5xx_hal_msp.cyclo ./Core/Src/stm32l5xx_hal_msp.d ./Core/Src/stm32l5xx_hal_msp.o ./Core/Src/stm32l5xx_hal_msp.su ./Core/Src/stm32l5xx_it.cyclo ./Core/Src/stm32l5xx_it.d ./Core/Src/stm32l5xx_it.o ./Core/Src/stm32l5xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32l5xx.cyclo ./Core/Src/system_stm32l5xx.d ./Core/Src/system_stm32l5xx.o ./Core/Src/system_stm32l5xx.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su

.PHONY: clean-Core-2f-Src

