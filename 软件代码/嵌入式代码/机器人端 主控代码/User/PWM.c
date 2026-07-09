// 推进器: 0    1    2    3           4    5 
// TIM8: PC6, PC7, PC8, PC9, TIM10: PB8, PB9
// 方向: 左前 左后 右前 右后         垂左  垂右
// H>1.5ms:     向外推             向上推(向下动)

#include "ch32v30x.h"
#include "debug.h"

#include "PWM.h"

// 内部函数：配置GPIO复用
static void PWM_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;

    // 使能相关GPIO时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    // TIM8: PC6, PC7, PC8, PC9, TIM10: PB8, PB9
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOB, &gpio);
}

// 内部函数：配置单个定时器
static void PWM_TIM_Init(TIM_TypeDef* TIMx, uint16_t channels)
{
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef oc;

    // 使能定时器时钟（TIM8/TIM10在APB2）
    if(TIMx == TIM8) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);}
    else if(TIMx == TIM10) {RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM10, ENABLE);}

    // 时基配置
    tim.TIM_Prescaler = PWM_PSC;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    tim.TIM_Period = PWM_ARR;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIMx, &tim);

    // PWM模式配置
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = PWM_DEFAULT_US - 1;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;

    // 启用对应通道
    if(channels & 0x01) TIM_OC1Init(TIMx, &oc);
    if(channels & 0x02) TIM_OC2Init(TIMx, &oc);
    if(channels & 0x04) TIM_OC3Init(TIMx, &oc);
    if(channels & 0x08) TIM_OC4Init(TIMx, &oc);

    if(channels & 0x01) TIM_OC1PreloadConfig(TIMx, TIM_OCPreload_Enable);
    if(channels & 0x02) TIM_OC2PreloadConfig(TIMx, TIM_OCPreload_Enable);
    if(channels & 0x04) TIM_OC3PreloadConfig(TIMx, TIM_OCPreload_Enable);
    if(channels & 0x08) TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);

    TIM_CtrlPWMOutputs(TIMx, ENABLE);  // 高级定时器必须开启主输出
    TIM_Cmd(TIMx, ENABLE);
}

// 公共接口：初始化6路PWM
void PWM_Init(void)
{
    uint8_t ch;
    PWM_GPIO_Init();
    PWM_TIM_Init(TIM8, 0x0F);   // CH1~CH4: PC6, PC7, PC8, PC9
    PWM_TIM_Init(TIM10, 0x03);  // CH1~CH2: PB8, PB9
    for(ch = 0; ch < 6; ch++) {PWM_SetDuty_us(ch, 1500);}  // 所有通道设为 1.5 ms
    printf("[PWM]>>> Init SUCCESS.\r\n");
}

// 公共接口：设置某通道占空比（us）
void PWM_SetDuty_us(uint8_t channel, uint16_t us)
{
    if(us < 1000) us = 1000;
    if(us > 2000) us = 2000;
    uint16_t reg_val = us - 1;

    switch (channel)
    {
        case 0: TIM8->CH1CVR = reg_val; break;  // T1: PA2
        case 1: TIM8->CH2CVR = reg_val; break;  // T2: PA3
        case 2: TIM8->CH3CVR = reg_val; break;  // T3: PA4
        case 3: TIM8->CH4CVR = reg_val; break;  // T4: PC4
        case 4: TIM10->CH1CVR = reg_val; break; // T5: PB8
        case 5: TIM10->CH2CVR = reg_val; break; // T6: PB9
        default: printf("[PWM]>>> Error: PWM_SetDuty_us() WRONG CHANNEL!(0-5)"); break;  // 打印信息：通道数错了
    }
}

// 测试函数：验证6路电机一致性
void PWM_Test(void)
{
    uint8_t ch;
    uint16_t us;

    for(ch = 0; ch < 6; ch++) {PWM_SetDuty_us(ch, 1500);}// 所有通道设为 1.5 ms
    Delay_Ms(100); // 稳定100ms

    // 依次测试每个通道
    for(ch = 0; ch < 6; ch++)
        {
        // a) 停->反转最大
        for(us = 1500; us >= 1000; us -= 5) {PWM_SetDuty_us(ch, us); Delay_Ms(20);}

        // b) 反转最大->正转最大
        for(us = 1000; us <= 2000; us += 5) {PWM_SetDuty_us(ch, us); Delay_Ms(20);}

        // c) 正转最大->停
        for(us = 2000; us >= 1500; us -= 5) {
            PWM_SetDuty_us(ch, us);
            Delay_Ms(20);
        }
        // 通道间稍作停顿
        Delay_Ms(100);
    }
    // 确保恢复所有通道到1.5ms
    for(ch = 0; ch < 6; ch++) {PWM_SetDuty_us(ch, 1500);}
}
