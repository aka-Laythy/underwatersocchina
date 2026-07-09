################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/Clock.c \
../User/DCMotor.c \
../User/StepMotor.c \
../User/ch32v30x_it.c \
../User/main.c \
../User/system_ch32v30x.c 

C_DEPS += \
./User/Clock.d \
./User/DCMotor.d \
./User/StepMotor.d \
./User/ch32v30x_it.d \
./User/main.d \
./User/system_ch32v30x.d 

OBJS += \
./User/Clock.o \
./User/DCMotor.o \
./User/StepMotor.o \
./User/ch32v30x_it.o \
./User/main.o \
./User/system_ch32v30x.o 



# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/0000----CH32V303CBT6-----光纤卷线机主控板代码/Debug" -I"d:/0-MounRiver-Projects/0000----CH32V303CBT6-----光纤卷线机主控板代码/Core" -I"d:/0-MounRiver-Projects/0000----CH32V303CBT6-----光纤卷线机主控板代码/User" -I"d:/0-MounRiver-Projects/0000----CH32V303CBT6-----光纤卷线机主控板代码/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
