// 以下是依赖项
#include "Tick.h"
#include "debug.h"

// 以下是需要被TICK调用的项
#include "Fiber.h"
#include "LinuxConnect.h"
// #include "Sensors.h"
extern volatile uint32_t i2c1_tick_ms;
extern volatile uint32_t i2c2_tick_ms;
extern volatile uint32_t pid_ticks_twent_ms;
extern volatile u8 main_loop_1s;

volatile u32 tick = 0;                     // 全局1ms心跳++

// 初始化TIM6为1ms滴答
void Tick_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    TIM_TimeBaseInitTypeDef tim;
    tim.TIM_Prescaler = 72-1;
    tim.TIM_Period = 2000-1;       // 144MHz - 1ms
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &tim);

    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = TIM6_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM6, ENABLE);
}

// TIM6 中断 - 1ms 心跳中断
// 可以处理很多事
// 比如处理：fiber嘀嗒（fiber_ticks_ms++）、fiber超时处理（Fiber_Process）、I2C超时处理（g_tick_ms++）
void TIM6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM6_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
        // 【注意这里面一定要耗时够短，一定要能“预测结果”】

        tick++;             // 【全局 tick 到处都可以用的心跳点】
        fiber_ticks_ms++;   // 【For Fiber】
        Fiber_Process();    // 【For Fiber】传入当前滴答
        LinuxConnect_CheckTimeout();     // 【for Linux 串口接收超时处理】
        i2c2_tick_ms++;     // 【For I2C】给MS5837的I2C超时保护计数器
        i2c1_tick_ms++;     // 【For I2C】给MMC5603的I2C超时保护计数器
        //gps_1ms_counter++;  // 【For GPS】DX-GP10

        if(tick%20 == 0)    // 每隔20ms 
        {
            pid_ticks_twent_ms++;   // 【for control, PID】
        }

        if(tick%1000 == 0)    // 每隔1s 
        {
            main_loop_1s = 1;// 【For 主循环 1s间隔进入】
        }
    }
}
