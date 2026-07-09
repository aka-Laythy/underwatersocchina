################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/RGB/RGB.c 

C_DEPS += \
./User/RGB/RGB.d 

OBJS += \
./User/RGB/RGB.o 



# Each subdirectory must supply rules for building sources it contributes
User/RGB/%.o: ../User/RGB/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/ch32v307-50hz2216/Debug" -I"d:/0-MounRiver-Projects/ch32v307-50hz2216/Core" -I"d:/0-MounRiver-Projects/ch32v307-50hz2216/User" -I"d:/0-MounRiver-Projects/ch32v307-50hz2216/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
