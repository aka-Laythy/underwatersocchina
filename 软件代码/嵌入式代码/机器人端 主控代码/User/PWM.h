#ifndef __PWM_H
#define __PWM_H

#include "ch32v30x.h"

/* ---------------- 用户可调宏 ---------------- */
#define PWM_FREQ_HZ     50
#define PWM_PERIOD_US   (1000000 / PWM_FREQ_HZ)   // 20000 us
#define PWM_DEFAULT_US  1500                      // 1.5 ms

// 定时器配置
#define PWM_PSC         (144 - 1)                 // 144 MHz / 144 = 1 MHz
#define PWM_ARR         (PWM_PERIOD_US - 1)       // 19999

// 通道数量
#define PWM_CHANNEL_NUM 6

void PWM_Init(void);
void PWM_SetDuty_us(uint8_t channel, uint16_t us);
void PWM_Test(void);

#endif /* __PWM_H */