################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Debug/debug.c 

C_DEPS += \
./Debug/debug.d 

OBJS += \
./Debug/debug.o 



# Each subdirectory must supply rules for building sources it contributes
Debug/%.o: ../Debug/%.c
	@	riscv-wch-elf-gcc -march=rv32ec_zmmul_xw -mabi=ilp32e -msmall-data-limit=0 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/0000----CH32V005F6P6-power-board-ble-pmos ---------- 电源板 最新/Debug" -I"d:/0-MounRiver-Projects/0000----CH32V005F6P6-power-board-ble-pmos ---------- 电源板 最新/Core" -I"d:/0-MounRiver-Projects/0000----CH32V005F6P6-power-board-ble-pmos ---------- 电源板 最新/User" -I"d:/0-MounRiver-Projects/0000----CH32V005F6P6-power-board-ble-pmos ---------- 电源板 最新/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
