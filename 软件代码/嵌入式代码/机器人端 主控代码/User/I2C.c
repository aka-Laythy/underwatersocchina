#include "I2C.h"
#include "debug.h"

// I2C1: MMC5603 (磁力计)
// I2C2: MS5837  (深度计)

// 软件模拟I2C 扫描I2C2总线设备，然后硬件初始化I2C2.
void IIC2_Scan_Soft(void)
{
    // 临时用 GPIO 模拟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);
    Delay_Ms(1);
    
    printf("[SW I2C2]>>> Scanning...\r\n");
    
    for(uint8_t addr = 0; addr < 128; addr++) {
        if((addr & 0x78) == 0 || (addr & 0x78) == 0x78) continue;
        
        // START
        GPIO_ResetBits(GPIOB, GPIO_Pin_11); Delay_Us(5);
        GPIO_ResetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
        
        // 发送地址
        for(int i = 7; i >= 0; i--) {
            if((addr << 1) & (1 << i)) GPIO_SetBits(GPIOB, GPIO_Pin_11);
            else GPIO_ResetBits(GPIOB, GPIO_Pin_11);
            Delay_Us(2);
            GPIO_SetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
            GPIO_ResetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
        }
        
        // 读 ACK
        GPIO_SetBits(GPIOB, GPIO_Pin_11);  // 释放 SDA
        Delay_Us(2);
        GPIO_SetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
        uint8_t ack = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0;
        GPIO_ResetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
        
        // STOP
        GPIO_ResetBits(GPIOB, GPIO_Pin_11); Delay_Us(2);
        GPIO_SetBits(GPIOB, GPIO_Pin_10); Delay_Us(5);
        GPIO_SetBits(GPIOB, GPIO_Pin_11); Delay_Us(5);
        
        if(ack) printf("[SW I2C2]>>> Found device at 0x%02X\r\n", addr);
        
        Delay_Ms(1);
    }
    
    // 恢复 I2C 模式
    IIC2_Init(400000);
}

/* ---------------- I2C2 初始化函数 ---------------- */
void IIC2_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};

    // 1. 使能 GPIOB 和 I2C2 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);

    // 2. 配置 PB10 (SCL) 和 PB11 (SDA) 为复用开漏输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;                  // 复用开漏（I2C 标准）
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 配置 I2C2
    I2C_InitStructure.I2C_ClockSpeed = bound;                        // 波特率
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_16_9;            // 快速模式下选I2C_DutyCycle_16_9，标准模式下选I2C_DutyCycle_2
    I2C_InitStructure.I2C_OwnAddress1 = HOST_ADDRESS;                // 主机模式下此地址无效，但需设置
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C2, &I2C_InitStructure);
    
    // 4. 使能 I2C2
    I2C_Cmd(I2C2, ENABLE);
}

void IIC2_Recovery(uint32_t bound)
{
    I2C_DeInit(I2C2);

    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                  // 复用开漏（I2C 标准）
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 先将两条线都置高
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);
    Delay_Us(5); // 小延时让线上电平稳定

    // 4. 关键：给SCL发送9个时钟脉冲，同时检测SDA是否被释放
    uint8_t i;
    for(i = 0; i < 9; i++) {
        // SCL低电平
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        Delay_Us(5);
        
        // SCL高电平  
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
        Delay_Us(5);
        
        // 检测SDA是否被从机释放（变高）
        if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 1)
        {
            break;// SDA已经释放，可以发送STOP条件了
        }
    }
    // 如果9个时钟后SDA还没释放，说明从机硬件故障

    // 5. 发送STOP条件：SCL高时，SDA从低到高跳变
    // 先确保SDA为低
    GPIO_ResetBits(GPIOB, GPIO_Pin_11);
    Delay_Us(5);
    // SCL保持高
    GPIO_SetBits(GPIOB, GPIO_Pin_10);  
    Delay_Us(5);
    // SDA变高，形成STOP
    GPIO_SetBits(GPIOB, GPIO_Pin_11);
    Delay_Us(5);

    IIC2_Init(bound);
}

/*
void IIC_Recovery(uint32_t bound)
{;}
*/

void IIC2_Scan(void)
{
    uint8_t addr, cnt = 0;
    printf("[HW I2C2]>>> Scanning...\r\n");
    
    for(addr = 0; addr < 128; addr++) {
        // 跳过保留地址
        if((addr & 0x78) == 0 || (addr & 0x78) == 0x78) continue;
        
        I2C_GenerateSTART(I2C2, ENABLE);
        while(!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT));
        
        I2C_Send7bitAddress(I2C2, addr << 1, I2C_Direction_Transmitter);
        
        // 等待ACK或超时
        uint32_t timeout = 10000;
        while(timeout--) {
            if(I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
            {
                printf("[HW I2C2]>>> Found device at 0x%02X\r\n", addr);
                cnt++;
                break;
            }
            if(I2C_GetFlagStatus(I2C2, I2C_FLAG_AF))
            {
                I2C_ClearFlag(I2C2, I2C_FLAG_AF);
                break;
            }
        }
        
        I2C_GenerateSTOP(I2C2, ENABLE);
        Delay_Ms(1);
    }
    printf("[HW I2C2]>>> Scan done! Total: %d Devices.\r\n", cnt);
}





/* ---------------- I2C1 初始化函数 ---------------- */
void IIC1_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};

    // 1. 使能 GPIOB 和 I2C1 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    // 2. 配置 PB6 (SCL) 和 PB7 (SDA) 为复用开漏输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;                  // 复用开漏（I2C 标准）
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 配置 I2C1
    I2C_InitStructure.I2C_ClockSpeed = bound;                        // 波特率
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_16_9;            // 快速模式下选I2C_DutyCycle_16_9，标准模式下选I2C_DutyCycle_2
    I2C_InitStructure.I2C_OwnAddress1 = HOST_ADDRESS;                // 主机模式下此地址无效，但需设置
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStructure);
    
    // 4. 使能 I2C1
    I2C_Cmd(I2C1, ENABLE);
}

void IIC1_Recovery(uint32_t bound)
{
    I2C_DeInit(I2C1);

    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                  // 复用开漏（I2C 标准）
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 先将两条线都置高
    GPIO_SetBits(GPIOB, GPIO_Pin_6 | GPIO_Pin_7);
    Delay_Us(5); // 小延时让线上电平稳定

    // 4. 关键：给SCL发送9个时钟脉冲，同时检测SDA是否被释放
    uint8_t i;
    for(i = 0; i < 9; i++) {
        // SCL低电平
        GPIO_ResetBits(GPIOB, GPIO_Pin_6);
        Delay_Us(5);
        
        // SCL高电平  
        GPIO_SetBits(GPIOB, GPIO_Pin_6);
        Delay_Us(5);
        
        // 检测SDA是否被从机释放（变高）
        if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7) == 1)
        {
            break;// SDA已经释放，可以发送STOP条件了
        }
    }
    // 如果9个时钟后SDA还没释放，说明从机硬件故障

    // 5. 发送STOP条件：SCL高时，SDA从低到高跳变
    // 先确保SDA为低
    GPIO_ResetBits(GPIOB, GPIO_Pin_7);
    Delay_Us(5);
    // SCL保持高
    GPIO_SetBits(GPIOB, GPIO_Pin_6);  
    Delay_Us(5);
    // SDA变高，形成STOP
    GPIO_SetBits(GPIOB, GPIO_Pin_7);
    Delay_Us(5);

    IIC1_Init(bound);
}

void IIC1_Scan(void)
{
    uint8_t addr, cnt = 0;
    printf("[HW I2C1]>>> Scanning...\r\n");
    
    for(addr = 0; addr < 128; addr++) {
        // 跳过保留地址
        if((addr & 0x78) == 0 || (addr & 0x78) == 0x78) continue;
        
        I2C_GenerateSTART(I2C1, ENABLE);
        while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT));
        
        I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
        
        // 等待ACK或超时
        uint32_t timeout = 10000;
        while(timeout--) {
            if(I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
            {
                printf("[HW I2C1]>>> Found device at 0x%02X\r\n", addr);
                cnt++;
                break;
            }
            if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF))
            {
                I2C_ClearFlag(I2C1, I2C_FLAG_AF);
                break;
            }
        }
        
        I2C_GenerateSTOP(I2C1, ENABLE);
        Delay_Ms(1);
    }
    printf("[HW I2C1]>>> Scan done! Total: %d Devices.\r\n", cnt);
}
