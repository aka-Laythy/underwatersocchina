#include "stick.h"

// 全局变量定义
Stick_Data_t Stick1 = {0};
Stick_Data_t Stick2 = {0};

// ADC通道定义 - 按扫描顺序排列
#define ADC_CH_COUNT    4
#define CH_STICK1_LR    ADC_Channel_1   // PA1 - 序列1
#define CH_STICK1_UD    ADC_Channel_0   // PA0 - 序列2  
#define CH_STICK2_LR    ADC_Channel_9   // PB1 - 序列3
#define CH_STICK2_UD    ADC_Channel_8   // PB0 - 序列4

// 按键引脚
#define STICK1_BTN_PIN  GPIO_Pin_9      // PB9
#define STICK2_BTN_PIN  GPIO_Pin_8      // PB8

// 参数
#define DEADZONE        50
#define CENTER          2048

// DMA缓冲区 - 必须全局静态，DMA直接写入
static volatile uint16_t ADC_DMA_Buffer[ADC_CH_COUNT] = {0};

// 私有变量
static uint8_t btn1_last = 1;
static uint8_t btn2_last = 1;
// static s16 Calibrattion_Val = 0;

// 私有函数
static uint16_t ApplyOffset(uint16_t raw, s16 offset);

/**
 * @brief 初始化摇杆 - ADC连续扫描+DMA模式
 */
void Stick_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | 
                          RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);  // 144MHz/8 = 18MHz
    
    // 2. 配置GPIO
    // PA0, PA1 模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // PB0, PB1 模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // PB8, PB9 上拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // 3. 复位并配置DMA1 Channel1 (ADC1对应DMA1 CH1)
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;  // ADC数据寄存器
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)ADC_DMA_Buffer;      // 内存缓冲区
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                    // 外设到内存
    DMA_InitStructure.DMA_BufferSize = ADC_CH_COUNT;                      // 4个通道
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;      // 外设地址固定
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;               // 内存地址递增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // 16位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;   // 16位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                       // 循环模式(关键！)
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                          // 非内存到内存
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    
    // 使能DMA
    DMA_Cmd(DMA1_Channel1, ENABLE);
    
    // 4. 复位并配置ADC
    ADC_DeInit(ADC1);
    
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;                          // 扫描模式(关键！)
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;                  // 连续转换(关键！)
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;   // 软件触发
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = ADC_CH_COUNT;                    // 4个通道
    ADC_Init(ADC1, &ADC_InitStructure);
    
    // 配置规则组通道序列 - 必须按顺序配置Rank 1-4
    ADC_RegularChannelConfig(ADC1, CH_STICK1_LR, 1, ADC_SampleTime_239Cycles5); // PA1
    ADC_RegularChannelConfig(ADC1, CH_STICK1_UD, 2, ADC_SampleTime_239Cycles5); // PA0
    ADC_RegularChannelConfig(ADC1, CH_STICK2_LR, 3, ADC_SampleTime_239Cycles5); // PB1
    ADC_RegularChannelConfig(ADC1, CH_STICK2_UD, 4, ADC_SampleTime_239Cycles5); // PB0
    
    // 关闭注入组，防止干扰
    ADC_ExternalTrigInjectedConvConfig(ADC1, ADC_ExternalTrigInjecConv_None);
    
    // 关闭数据缓冲器(沁恒特有)
    ADC_BufferCmd(ADC1, DISABLE);
    
    // 使能DMA请求(关键！)
    ADC_DMACmd(ADC1, ENABLE);
    
    // 5. 校准ADC
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    //while(ADC_GetCalibrationStatus(ADC1));        // 会卡死在这里，直接注释掉了
    //Calibrattion_Val = Get_CalibrationValue(ADC1);
    
    // 使能ADC
    ADC_Cmd(ADC1, ENABLE);
    
    // 6. 配置TIM2 (1ms中断，用于按键扫描和数据处理)
    TIM_InitStructure.TIM_Period = 72 - 1;      // 144MHz/1000/72 = 2kHz? 不对，重新计算
    // 144MHz APB1 = 72MHz (APB1 max 72MHz)
    // 72MHz / 1000 / 72 = 1000Hz = 1ms
    TIM_InitStructure.TIM_Prescaler = 1000 - 1;
    TIM_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_InitStructure);
    
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    
    // 7. NVIC配置
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void Stick_Start(void)
{
    // 启动ADC连续转换
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    // 启动定时器
    TIM_Cmd(TIM2, ENABLE);
}

void Stick_Stop(void)
{
    TIM_Cmd(TIM2, DISABLE);
    ADC_SoftwareStartConvCmd(ADC1, DISABLE);
}

/**
 * @brief TIM2中断 - 1ms周期，处理按键和ADC数据解析
 * @note DMA在后台自动刷新ADC_DMA_Buffer，这里只需读取并处理
 */
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void)
{
    uint8_t btn;
    
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET) return;
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    
    // 直接从DMA缓冲区读取ADC值（DMA已自动更新）
    // 缓冲区顺序: [0]=Stick1_LR, [1]=Stick1_UD, [2]=Stick2_LR, [3]=Stick2_UD
    Stick1.x = ApplyOffset(ADC_DMA_Buffer[0], 11);  // 后面+-的数都是机械零点误差
    Stick1.y = ApplyOffset(ADC_DMA_Buffer[1], -5);
    Stick2.x = ApplyOffset(ADC_DMA_Buffer[2], -28);
    Stick2.y = ApplyOffset(ADC_DMA_Buffer[3], 11);
    
    
    // 摇杆1按键 (PB9)
    btn = GPIO_ReadInputDataBit(GPIOB, STICK1_BTN_PIN);
    Stick1.button = (btn == Bit_RESET) ? 1 : 0;
    Stick1.btn_press = (btn1_last == 1 && btn == 0) ? 1 : 0;
    Stick1.btn_release = (btn1_last == 0 && btn == 1) ? 1 : 0;
    btn1_last = btn;
    
    // 摇杆2按键 (PB8)
    btn = GPIO_ReadInputDataBit(GPIOB, STICK2_BTN_PIN);
    Stick2.button = (btn == Bit_RESET) ? 1 : 0;
    Stick2.btn_press = (btn2_last == 1 && btn == 0) ? 1 : 0;
    Stick2.btn_release = (btn2_last == 0 && btn == 1) ? 1 : 0;
    btn2_last = btn;
}

// 应用校准值
static uint16_t ApplyOffset(uint16_t raw, s16 offset)
{
    s16 result = (s16)raw + offset;
    if(result < 0) return 0;
    if(result > 4095) return 4095;
    return (uint16_t)result;
}

/**
 * @brief  ADC值(0~4095)映射到控制量(-50~+50)，带死区
 * @param  adc: 输入ADC值 (0-4095)
 * @param  deadband: 回零误差，如20表示2048±20范围内都返回0
 * @retval 控制量 (-50 ~ +50)
 * @note   实际输出范围约-50~+49（因整数除法），但对控制精度无影响
 */
int8_t ADC_Map(int16_t adc, uint8_t deadband)
{
    if (adc < 0) return -50;
    if (adc > 4095) return 50;
    
    int32_t offset = (int32_t)adc - 2048;  // 转32位防止溢出
    int32_t db = deadband;
    int32_t max_range = 2048 - db;         // 正/负半轴最大幅度
    
    if (offset > db)
        return (int8_t)((offset - db) * 50 / max_range);
    
    if (offset < -db)
        return (int8_t)((offset + db) * 50 / max_range);
    
    return 0;
}