################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += 

OBJS += 

C_DEPS += 


# Each subdirectory must supply rules for building sources it contributes
Drivers/BFR_Krill_Drivers/src/%.o Drivers/BFR_Krill_Drivers/src/%.su Drivers/BFR_Krill_Drivers/src/%.cyclo: ../Drivers/BFR_Krill_Drivers/src/%.c Drivers/BFR_Krill_Drivers/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L552xx -c -I../Core/Inc -I../Drivers/BFR_Krill_Drivers/Inc -I../Drivers/STM32L5xx_HAL_Driver/Inc -I../Drivers/STM32L5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L5xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-BFR_Krill_Drivers-2f-src

clean-Drivers-2f-BFR_Krill_Drivers-2f-src:

.PHONY: clean-Drivers-2f-BFR_Krill_Drivers-2f-src

