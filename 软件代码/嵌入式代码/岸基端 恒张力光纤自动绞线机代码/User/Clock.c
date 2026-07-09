#include "Clock.h"
#include "ch32v30x.h"

volatile u32 tick_ms = 0;

// 初始化TIM3为1ms滴答
void Clock_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    TIM_TimeBaseInitTypeDef tim;
    tim.TIM_Prescaler = 72-1;     // 主频144Mhz这样配置是1ms
    tim.TIM_Period = 2000-1;       // 1ms
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &tim);

    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = TIM3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM3, ENABLE);
}

// TIM3 中断 - 1ms 心跳中断
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        tick_ms++;
    }
}
