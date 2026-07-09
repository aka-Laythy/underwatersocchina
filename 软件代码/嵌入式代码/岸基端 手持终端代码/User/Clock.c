#include "Clock.h"
#include "ch32v30x.h"
//#include "ch32v30x_misc.h"

#include "stick.h"
#include "Fiber.h"

#define ADC_MAP_DEADZONE   30

u32 tick_ms = 0;
extern RemoteControl_Packet rc_packet;
extern Stick_Data_t Stick1;
extern Stick_Data_t Stick2;

// 初始化TIM1为1ms滴答
void Clock_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInitTypeDef tim;
    tim.TIM_Prescaler = 72-1;     // 主频144Mhz这样配置是1ms
    tim.TIM_Period = 2000-1;       // 1ms
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &tim);

    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = TIM1_UP_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM1, ENABLE);
}

// TIM1 中断 - 1ms 心跳中断
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        // 【注意这里面一定要耗时够短，一定要能“预测结果”】
        tick_ms++;
        if(tick_ms%10==0)
        {
            // 间隔10ms进入，发送一帧光纤遥控包
            rc_packet.x = ADC_Map(Stick1.x, ADC_MAP_DEADZONE);      // 【注，这个顺序是对的，严格按照这个来对应xyz - 机器人端的坐标轴】
            rc_packet.y = ADC_Map(Stick1.y, ADC_MAP_DEADZONE);
            rc_packet.z = ADC_Map(Stick2.y, ADC_MAP_DEADZONE);
            rc_packet.yaw = ADC_Map(Stick2.x, ADC_MAP_DEADZONE);
            Fiber_Send_RemoteControl(rc_packet.x, rc_packet.y, rc_packet.z, rc_packet.yaw);
        }
    }
}
