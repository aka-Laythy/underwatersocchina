#ifndef __STICK_H
#define __STICK_H

#include "ch32v30x.h"

// 摇杆数据结构
typedef struct {
    int16_t x;          // 左右值: -2048 ~ +2047
    int16_t y;          // 上下值: -2048 ~ +2047
    uint8_t button;     // 按键状态: 0=未按下, 1=按下
    uint8_t btn_press;  // 按下触发标志(读取后需手动清零)
    uint8_t btn_release;// 释放触发标志(读取后需手动清零)
} Stick_Data_t;

// 函数声明
void Stick_Init(void);      // 初始化GPIO、ADC(DMA连续扫描)、TIM2定时器
void Stick_Start(void);     // 启动DMA和定时器
void Stick_Stop(void);      // 停止DMA和定时器
int8_t ADC_Map(int16_t adc, uint8_t deadband);

#endif