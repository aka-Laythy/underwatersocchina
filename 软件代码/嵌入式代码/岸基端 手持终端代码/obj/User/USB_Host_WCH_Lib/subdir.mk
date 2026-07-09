################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/USB_Host_WCH_Lib/app_km.c \
../User/USB_Host_WCH_Lib/ch32v30x_usbfs_host.c \
../User/USB_Host_WCH_Lib/usb_host_hid.c \
../User/USB_Host_WCH_Lib/usb_host_hub.c 

C_DEPS += \
./User/USB_Host_WCH_Lib/app_km.d \
./User/USB_Host_WCH_Lib/ch32v30x_usbfs_host.d \
./User/USB_Host_WCH_Lib/usb_host_hid.d \
./User/USB_Host_WCH_Lib/usb_host_hub.d 

OBJS += \
./User/USB_Host_WCH_Lib/app_km.o \
./User/USB_Host_WCH_Lib/ch32v30x_usbfs_host.o \
./User/USB_Host_WCH_Lib/usb_host_hid.o \
./User/USB_Host_WCH_Lib/usb_host_hub.o 



# Each subdirectory must supply rules for building sources it contributes
User/USB_Host_WCH_Lib/%.o: ../User/USB_Host_WCH_Lib/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Debug" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Core" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/User" -I"d:/0-MounRiver-Projects/000001-----CH32V303CBT6-oled-joystick-sim-mqtt ---------- 遥控板 最新/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
