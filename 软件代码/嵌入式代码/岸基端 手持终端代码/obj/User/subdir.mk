################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/Clock.c \
../User/Fiber.c \
../User/OLED.c \
../User/OLED_Data.c \
../User/SIM.c \
../User/ch32v30x_it.c \
../User/main.c \
../User/stick.c \
../User/system_ch32v30x.c 

C_DEPS += \
./User/Clock.d \
./User/Fiber.d \
./User/OLED.d \
./User/OLED_Data.d \
./User/SIM.d \
./User/ch32v30x_it.d \
./User/main.d \
./User/stick.d \
./User/system_ch32v30x.d 

OBJS += \
./User/Clock.o \
./User/Fiber.o \
./User/OLED.o \
./User/OLED_Data.o \
./User/SIM.o \
./User/ch32v30x_it.o \
./User/main.o \
./User/stick.o \
./User/system_ch32v30x.o 



# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Debug" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Core" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/User" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
