################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/Control.c \
../User/Fiber.c \
../User/FuzzyPID.c \
../User/GPS.c \
../User/I2C.c \
../User/JY61P.c \
../User/LinuxConnect.c \
../User/MMC5603.c \
../User/MS5837.c \
../User/PWM.c \
../User/PwrBoard.c \
../User/SPI_Flash.c \
../User/SysFlow.c \
../User/Tick.c \
../User/ch32v30x_it.c \
../User/main.c \
../User/system_ch32v30x.c 

C_DEPS += \
./User/Control.d \
./User/Fiber.d \
./User/FuzzyPID.d \
./User/GPS.d \
./User/I2C.d \
./User/JY61P.d \
./User/LinuxConnect.d \
./User/MMC5603.d \
./User/MS5837.d \
./User/PWM.d \
./User/PwrBoard.d \
./User/SPI_Flash.d \
./User/SysFlow.d \
./User/Tick.d \
./User/ch32v30x_it.d \
./User/main.d \
./User/system_ch32v30x.d 

OBJS += \
./User/Control.o \
./User/Fiber.o \
./User/FuzzyPID.o \
./User/GPS.o \
./User/I2C.o \
./User/JY61P.o \
./User/LinuxConnect.o \
./User/MMC5603.o \
./User/MS5837.o \
./User/PWM.o \
./User/PwrBoard.o \
./User/SPI_Flash.o \
./User/SysFlow.o \
./User/Tick.o \
./User/ch32v30x_it.o \
./User/main.o \
./User/system_ch32v30x.o 



# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6 - 0000000最新 - 副本/Debug" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6 - 0000000最新 - 副本/Core" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6 - 0000000最新 - 副本/User" -I"d:/0-MounRiver-Projects/000000------AAAPROJECT-CH32V307VCT6 - 0000000最新 - 副本/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
