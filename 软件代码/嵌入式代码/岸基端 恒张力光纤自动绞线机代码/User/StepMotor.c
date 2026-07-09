#include "debug.h"
#include "StepMotor.h"

/*
光电门注释：
低电平：到达限位，应该停止或者换向。高电平：空闲状态。
电机侧光电门：PA12，远端侧光电门：PC14。
step_dir=0向电机侧运动，step_dir=1向远端侧运动。
*/

extern volatile u32 tick_ms;
volatile u32 step_current = 0;   // 当前已发脉冲数
volatile u32 step_target  = 0;   // 目标总脉冲数
volatile u8  step_dir = 1;	     // 当前方向


void motor_42_init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
	GPIO_InitTypeDef gpio;
	gpio.GPIO_Mode = GPIO_Mode_Out_PP;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	gpio.GPIO_Pin = GPIO_Pin_11;
	GPIO_Init(GPIOA, &gpio);
	gpio.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_Init(GPIOB, &gpio);
	gpio.GPIO_Mode = GPIO_Mode_AF_PP;
	gpio.GPIO_Pin = GPIO_Pin_8;
	GPIO_Init(GPIOA, &gpio);
	GPIO_ResetBits(GPIOB, GPIO_Pin_12 | GPIO_Pin_13);
	GPIO_ResetBits(GPIOA, GPIO_Pin_11);								  // enable
	GPIO_SetBits(GPIOB, GPIO_Pin_14);								  // 16 microsteps
	motor_42_set_dir(step_dir);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;                    // 定时器基础配置
    TIM_TimeBaseStructure.TIM_Prescaler     = 144-1;			      // psc
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_Period        = 50-1;                 // arr , 1000hz / 1ms.
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;            // 输出比较通道1：pwm1 mode
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_Pulse       = 25;                        // ccr, 50% duty
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
	TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);

    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);    //  改用更新中断（每个完整周期触发1次）
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, DISABLE);  // 初始关闭
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM1_UP_IRQn;  // NVIC: 注意更新中断使用 TIM1_UP_IRQn
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void ir_limit_init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	GPIO_InitTypeDef gpio;
	gpio.GPIO_Mode = GPIO_Mode_IPU;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14;
	GPIO_Init(GPIOC, &gpio);
//	gpio.GPIO_Pin = GPIO_Pin_12;
//	GPIO_Init(GPIOA, &gpio);
}


void motor_42_run(u32 steps)
{
    step_current = 0;        // 清空计数
    step_target  = steps;    // 设置目标步数
    TIM_Cmd(TIM1, ENABLE);   // 启动定时器 → 开始发脉冲
}

void motor_42_Stop(void)
{
    TIM_Cmd(TIM1, DISABLE);  // 关闭定时器
    step_current = 0;
    step_target  = 0;
}

void motor_42_set_dir(u8 dir)
{
	GPIO_WriteBit(GPIOB, GPIO_Pin_15, (BitAction)dir);
}

// 非阻塞限位检测：100ms窗口，10ms采样间隔，低电平≥80%触发
// pin: GPIO_Pin_13(PC13近端) 或 GPIO_Pin_14(PC14远端)
// 返回: 1=确认触发限位，0=未触发
u8 limit_check_debounce(u16 pin)
{
    // 静态变量：双通道独立 [0]=PC13, [1]=PC14
    static u32 last_tick[2] = {0};          // 上次采样时刻
    static u8  fifo[2][10] = {{0}};         // 10点滑动窗口(100ms/10ms)
    static u8  wr_idx[2] = {0};             // 写指针
    static u8  ready[2] = {0};              // 缓冲填满标志
    u8 ch = (pin == GPIO_Pin_13) ? 0 : 1;   // 通道映射
    u32 now = tick_ms;
    // ① 节流：每10ms最多采样1次（完全非阻塞）
    if(now - last_tick[ch] < 10) return 0;
    last_tick[ch] = now;
    // ② 采样当前电平：0=低(触发), 1=高(空闲)
    u8 lvl = (GPIO_ReadInputDataBit(GPIOC, pin) == Bit_RESET) ? 0 : 1;
    // ③ 循环写入滑动窗口（自动覆盖最旧）
    fifo[ch][wr_idx[ch]] = lvl;
    wr_idx[ch] = (wr_idx[ch] + 1) % 10;
    if(wr_idx[ch] == 0) ready[ch] = 1;      // 写满一轮
    // ④ 缓冲未满不判定（避免上电误触发）
    if(!ready[ch]) return 0;
    // ⑤ 统计低电平占比
    u8 low_cnt = 0;
    for(u8 i = 0; i < 10; i++) if(fifo[ch][i] == 0) low_cnt++;
    // ⑥ ≥80%低电平 → 确认触发
    return (low_cnt >= 8) ? 1 : 0;
}

void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void)
{
    // 检查是否为UP中断
    if(TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        step_current++;                                             // 每次中断 = 1个完整脉冲
        if(step_current >= step_target) {TIM_Cmd(TIM1, DISABLE);}   // 精确停止
    }
}
