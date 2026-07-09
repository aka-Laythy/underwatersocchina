#include "debug.h"
#include "ch32v00X_adc.h"
#include "main.h"

volatile u32 tick = 0;                      // 全局tick时钟
volatile u32 last_tick = 0;                 // 主循环状态机用
volatile u32 last_tick_read_adc = 0;        // 主循环ADC调用
volatile u8  pwr_state = 0;                 // power state

volatile uint8_t  g_usart2_rx_byte = 0;     // 接收到的单字节
volatile uint8_t  g_usart2_rx_flag = 0;     // 接收标志（1=有新数据）
volatile uint8_t  g_usart2_tx_buf[20];      // 发送缓冲区（不超过20字节含\0）
volatile uint8_t  g_usart2_tx_idx  = 0;     // 发送索引
volatile uint8_t  g_usart2_tx_len  = 0;     // 发送长度
volatile uint8_t  g_usart2_tx_busy = 0;     // 发送忙标志（1=正在发送）

volatile u32 i_bussense_ma = 0;   // IBUS:   PD4/A7,  0.5mΩ检流,  满量程约66A
volatile u32 i_5vsense_ma  = 0;   // I5V:    PD5/A5,  3mΩ检流,    满量程约11A  
volatile u32 i_12vsense_ma = 0;   // I12V:   PD6/A6,  6mΩ检流,    满量程约5.5A
static u32 lp_bus_history  = 0;   // 滤波用，历史值
static u32 lp_5v_history   = 0;
static u32 lp_12v_history  = 0;
#define LP_FILTER_SHIFT      4    // 滤波系数：越大越平滑，响应越慢。推荐8~32. 右移4位 = 除以16，即新值权重1/16

void TIM2_Init(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    // 1. 使能TIM2时钟 (APB1总线)
    RCC_PB1PeriphClockCmd(RCC_PB1Periph_TIM2, ENABLE);
    // 2. 定时器时基配置
    TIM_TimeBaseStructure.TIM_Period = arr;              // 自动重装载值
    TIM_TimeBaseStructure.TIM_Prescaler = psc;           // 预分频器
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 时钟分频因子
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    // 3. 使能更新中断 (UIE - Update Interrupt Enable)
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    // 4. NVIC中断优先级配置
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;      // TIM2中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;   // 子优先级0
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;      // 使能中断通道
    NVIC_Init(&NVIC_InitStructure);
    // 5. 启动定时器
    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)  // 检查更新中断标志
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);     // 必须手动清除中断标志
        tick++;
    }
}

void USART1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOC | RCC_PB2Periph_USART1 | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap3_USART1, ENABLE);
    /* USART1_3 TX-->C.0   RX-->C.1 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

void USART2_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_USART2 | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap3_USART2, ENABLE);
    /* USART2_3 TX-->D.2   RX-->D.3 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;           // USART2中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;   // 抢占优先级1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;          // 子优先级1
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

void BLE_Init(void)
{
    USART2_Init();
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio);
    GPIO_WriteBit(GPIOC, GPIO_Pin_5, Bit_RESET);
}

void USART2_SendString(uint8_t *str)
{
    uint8_t i;
    while(g_usart2_tx_busy);    //等待上一次发送完成（阻塞式等待，也可改为返回0/1表示是否成功）
    for(i = 0; i < 19; i++)     // 复制字符串到发送缓冲区（最多19字节，保留1字节给\0）
    {
        if(str[i] == '\0') break;
        g_usart2_tx_buf[i] = str[i];
    }
    g_usart2_tx_len = i;        // 实际发送长度（不含\0）
    g_usart2_tx_idx = 0;        // 索引归零
    g_usart2_tx_busy = 1;       // 标记发送忙
    if(g_usart2_tx_len > 0)     // 先发送第一个字节，剩余字节在TXE中断中发送
    {
        USART_SendData(USART2, g_usart2_tx_buf[0]);
        g_usart2_tx_idx = 1;
        USART_ITConfig(USART2, USART_IT_TXE, ENABLE);  // 开启发送空中断
    }
    else
    {
        g_usart2_tx_busy = 0;   // 空字符串直接置闲
    }
}

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)    // ----- 接收中断（RXNE） -----
    {
        g_usart2_rx_byte = USART_ReceiveData(USART2);        // 读取数据（自动清RXNE标志）
        g_usart2_rx_flag = 1;                                // 置位新数据标志
    }
    if(USART_GetITStatus(USART2, USART_IT_TXE) != RESET)     // ----- 发送中断（TXE） -----
    {
        if(g_usart2_tx_idx < g_usart2_tx_len)
        {
            USART_SendData(USART2, g_usart2_tx_buf[g_usart2_tx_idx]);   // 写DR自动清除TXE标志
            g_usart2_tx_idx++;
        }
        else
        {

            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);              // 发送完成，关闭TXE中断，置闲
            g_usart2_tx_busy = 0;
        }

    }
}

void ISense_ADC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    ADC_InitTypeDef ADC_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_ADC1, ENABLE);    // 开启时钟（CH32V005使用PB2命名） 

    RCC_ADCCLKConfig(RCC_PCLK2_Div4);  // 配置ADC时钟分频：系统时钟48MHz，必须分频到≤14MHz，推荐Div4=12MHz
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6;    // 配置GPIO为模拟输入（AIN）: PD4(ADC7-I12V), PD5(ADC5-I5V), PD6(ADC6-IBUS)
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;  // 必须设为模拟输入，关闭数字功能
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;              // 独立模式
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;                   // 单次单通道（手动切换）
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;             // 单次转换模式
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; // 软件触发
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;          // 右对齐（12位数据在bit11:0）
    ADC_InitStructure.ADC_NbrOfChannel = 1;                         // 规则组通道数=1
    ADC_Init(ADC1, &ADC_InitStructure);
    ADC_Cmd(ADC1, ENABLE);    //使能ADC并执行校准（沁恒ADC必需步骤）
    Delay_Us(30);
}

u16 ISense_ReadSingleChannel(u8 channel)
{
    u32 timeout = 100000;  // 超时计数，约10ms@48MHz
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_CyclesMode7);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)     // 等待EOC，带超时退出
    {
        if(--timeout == 0)
        {
            printf("ERR>>> ADC Timeout on Ch%d!\r\n", channel);
            return 0;  // 超时返回0
        }
    }
    
    return ADC_GetConversionValue(ADC1);
}

void ISense_ADC_Trig(void)
{
    u16 adc_raw;
    adc_raw = ISense_ReadSingleChannel(ADC_Channel_6);    // 通道1: IBUS - PD6 - ADC_Channel_6 - 0.5mΩ
    i_bussense_ma = ((u32)adc_raw * 3300) / 205;          // I(mA) = ADC * 3300 / 4096 / (100 * 0.0005) = ADC * 3300 / 205
    adc_raw = ISense_ReadSingleChannel(ADC_Channel_5);    // 通道2: I5V - PD5 - ADC_Channel_5 - 3mΩ
    i_5vsense_ma = ((u32)adc_raw * 3300) / 1229;          // I(mA) = ADC * 3300 / 4096 / (100 * 0.003) = ADC * 3300 / 1229
    adc_raw = ISense_ReadSingleChannel(ADC_Channel_7);    // 通道3: I12V - PD4 - ADC_Channel_7 - 6mΩ
    i_12vsense_ma = ((u32)adc_raw * 3300) / 2458;         // I(mA) = ADC * 3300 / 4096 / (100 * 0.006) = ADC * 3300 / 2458
    // 应用平滑滤波（直接覆盖全局变量为平滑值）
    ISense_Filter_Apply();
}

/**
 * @brief 一阶低通滤波（整数实现，无除法，仅移位）
 * @param history 历史值指针（存储放大后的值）
 * @param input 新输入值（原始mA值）
 * @return 滤波后的平滑值
 * @note  公式: Y[n] = Y[n-1] + (X[n] - Y[n-1]) / 16
 *        等效于: Y[n] = (1/16)*X[n] + (15/16)*Y[n-1]
 */
static u32 LowPass_Update(u32 *history, u32 input)
{
    if(*history == 0) {
        // 首次初始化，直接赋值（避免从0缓慢爬升）
        *history = input << LP_FILTER_SHIFT;
        return input;
    }
    
    // 定点数计算: history = history - (history>>SHIFT) + input
    // 即: New_Sum = Old_Sum - Old_Sum/16 + New_Input
    *history = *history - (*history >> LP_FILTER_SHIFT) + input;
    
    // 返回实际值: Sum / 16
    return *history >> LP_FILTER_SHIFT;
}

/**
 * @brief 重置滤波器（首次上电或需要清空历史时调用）
 */
void ISense_Filter_Reset(void)
{
    lp_bus_history = 0;
    lp_5v_history = 0;
    lp_12v_history = 0;
}

/**
 * @brief 对全局电流值进行平滑滤波
 * @note  直接修改 i_bussense_ma 等全局变量为平滑后的值
 *        如需保留原始值用于调试，请在调用前复制到临时变量
 */
void ISense_Filter_Apply(void)
{
    i_bussense_ma = LowPass_Update(&lp_bus_history, i_bussense_ma);
    i_5vsense_ma  = LowPass_Update(&lp_5v_history,  i_5vsense_ma);
    i_12vsense_ma = LowPass_Update(&lp_12v_history, i_12vsense_ma);
}

void Power_Switch_GPIO_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_RESET);    // 默认下拉 - 关闭主电源
}

void Power_Switch(u8 switch_set, u8 new_state)
{
    if(switch_set==1)
    {
        if(new_state==1)
        {
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_SET);      // 上拉 - 开启主电源
        }
        else
        {
            GPIO_WriteBit(GPIOC, GPIO_Pin_3, Bit_RESET);    // 默认下拉 - 关闭主电源
        }
    }
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    __enable_irq();

    TIM2_Init(999, 47);     // 48MHz主频下实现1ms定时中断
    Delay_Init();
    USART1_Init();
    BLE_Init();
    ISense_ADC_Init();
    ISense_Filter_Reset();  // 清空滤波历史值
    Power_Switch_GPIO_Init();
    printf("SYS>>> SystemClk:%d\r\n",SystemCoreClock);
    printf("SYS>>> ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    while(1)
    {
        if(g_usart2_rx_byte == 0x66)
        {
            g_usart2_rx_byte = 0x00;
            pwr_state = 1;
            Power_Switch(1, pwr_state);
            printf("SYS>>> Main Power ON!\r\n");
        }
        if(g_usart2_rx_byte == 0x77)
        {
            g_usart2_rx_byte = 0x00;
            pwr_state = 0;
            Power_Switch(1, pwr_state);
            printf("SYS>>> Main Power OFF!\r\n");
        }
        if(tick - last_tick_read_adc >= 10)
        {
            last_tick_read_adc = tick;
            ISense_ADC_Trig();
        }
        if(tick - last_tick >= 1000)    // 坑：tick这个变量必须给volatile修饰！否则必出问题。
        {
            last_tick = tick;
            ISense_ADC_Trig();
            printf("SYS>>> %06ds.\r\n", tick/1000);
            printf("PWR>>> VBUS:%05dmA, 5V:%04dmA, 12V:%04dmA.\r\n", i_bussense_ma, i_5vsense_ma, i_12vsense_ma);
        }
    }
}
