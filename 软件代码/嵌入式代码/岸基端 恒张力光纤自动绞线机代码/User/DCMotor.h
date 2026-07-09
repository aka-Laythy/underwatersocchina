#ifndef __DCMOTOR_H
#define __DCMOTOR_H

#include "ch32v30x.h"

// 方向定义
typedef enum {
    MOTOR_DIR_STOP    = 0,  // 停止（Coast/高阻态）
    MOTOR_DIR_FORWARD = 1,  // 正转（IN1 PWM, IN2 0）
    MOTOR_DIR_REVERSE = 2   // 反转（IN1 0, IN2 PWM）
} MotorDirection_t;

// 初始化与基础控制
void DCMotor_Init(void);
void DCMotor_Stop(void);    // 进入 Coast 模式（低功耗，IN1=IN2=0）

// 解耦控制接口：方向与速度完全独立
void DCMotor_SetDirection(MotorDirection_t dir);
void DCMotor_SetSpeed(uint8_t percent);  // 输入范围 0-100%

// 便捷接口：同时设置方向和速度
void DCMotor_Run(MotorDirection_t dir, uint8_t speed);

// 刹车模式（Brake：IN1=IN2=1，快速制动）
void DCMotor_Brake(void);

#endif