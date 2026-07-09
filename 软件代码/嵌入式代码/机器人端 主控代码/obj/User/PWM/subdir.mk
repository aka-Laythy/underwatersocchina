################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/PWM/PWM.c 

C_DEPS += \
./User/PWM/PWM.d 

OBJS += \
./User/PWM/PWM.o 



# Each subdirectory must supply rules for building sources it contributes
User/PWM/%.o: ../User/PWM/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6/Debug" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6/Core" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6/User" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
