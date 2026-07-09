################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/USBTouch/Touch.c \
../User/USBTouch/app_km.c \
../User/USBTouch/ch32v30x_usbfs_host.c \
../User/USBTouch/usb_host_hid.c \
../User/USBTouch/usb_host_hub.c \
../User/USBTouch/usb_touch_relay.c 

C_DEPS += \
./User/USBTouch/Touch.d \
./User/USBTouch/app_km.d \
./User/USBTouch/ch32v30x_usbfs_host.d \
./User/USBTouch/usb_host_hid.d \
./User/USBTouch/usb_host_hub.d \
./User/USBTouch/usb_touch_relay.d 

OBJS += \
./User/USBTouch/Touch.o \
./User/USBTouch/app_km.o \
./User/USBTouch/ch32v30x_usbfs_host.o \
./User/USBTouch/usb_host_hid.o \
./User/USBTouch/usb_host_hub.o \
./User/USBTouch/usb_touch_relay.o 



# Each subdirectory must supply rules for building sources it contributes
User/USBTouch/%.o: ../User/USBTouch/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Debug" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Core" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/User" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
