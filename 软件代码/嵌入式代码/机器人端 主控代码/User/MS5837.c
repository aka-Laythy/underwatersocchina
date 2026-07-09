#include "ch32v30x.h"
#include "debug.h"
#include "I2C.h"
#include <math.h>

#include "MS5837.h"

volatile uint32_t i2c2_tick_ms = 0;     // 1ms心跳计数器，放在1ms中断里++，I2C通讯超时用

// 校正系数存储变量
volatile MS5837_Calibration_t g_calibration = {0};
// 对外数据接口变量
MS5837_Data_t sensor_data = {0, 0, 0, 0}; // 最后，0=invalid，1=valid

// 辅助函数：等待 I2C 事件带超时
// 实测：SDA线插拔可以恢复，SCL线插拔不可恢复！
// 实对于SCL线插拔，应该增加其他保护措施！
static uint8_t I2C_WaitEventTimeout(I2C_TypeDef* I2Cx, uint32_t event, uint32_t timeout_ms)
{
    uint32_t start_tick = i2c2_tick_ms;
    while (!I2C_CheckEvent(I2Cx, event))
    {
        if ((i2c2_tick_ms - start_tick) >= timeout_ms)
        {
            //I2C_ClearFlag(I2Cx, I2C_FLAG_AF);  // 必须加这行！
            //I2C_DeInit(I2C2);
            //RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, DISABLE);
            //IIC_Init(400000);
            IIC2_Recovery(400000);
            return 0; // 超时
        }
    }
    return 1; // 成功
}

// 辅助函数：等待 I2C 标志带超时
static uint8_t I2C_WaitFlagTimeout(I2C_TypeDef* I2Cx, uint32_t flag, FlagStatus status, uint32_t timeout_ms)
{
    uint32_t start_tick = i2c2_tick_ms;
    while (I2C_GetFlagStatus(I2Cx, flag) == status)
    {
        if ((i2c2_tick_ms - start_tick) >= timeout_ms)
        {
            //I2C_ClearFlag(I2Cx, I2C_FLAG_AF);  // 必须加这行！
            //I2C_DeInit(I2C2);
            //RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, DISABLE);
            IIC2_Recovery(400000);
            return 0; // 超时
        }
    }
    return 1; // 成功
}

void MS5837_Reset(void)
{
    // 等待总线空闲（带超时）
    if (!I2C_WaitFlagTimeout(I2C2, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> Reset: Bus busy timeout!\r\n");
        return;
    }
    
    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> Reset: START timeout!\r\n");
        return;
    }
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> Reset: Address timeout!\r\n");
        return;   // 260215之前是注释掉了这个return的
    }
    
    I2C_SendData(I2C2, CMD_MS5837_RESET);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> Reset: Data timeout!\r\n");
        return;   // 260215之前是注释掉了这个return的
    }
    
    I2C_GenerateSTOP(I2C2, ENABLE);
    //Delay_Ms(1);
    //Delay_Us(100);
}

uint16_t MS5837_ReadPROM(uint8_t prom_addr)
{
    uint16_t data = 0;
    
    if (!I2C_WaitFlagTimeout(I2C2, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS))
    {
        return 0xFFFF; // 超时返回错误值
    }

    // 写命令
    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFFFF;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFFFF;
    
    I2C_SendData(I2C2, prom_addr);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return 0xFFFF;
    
    I2C_GenerateSTOP(I2C2, ENABLE);

    // 读2字节
    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFFFF;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_READ, I2C_Direction_Receiver);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFFFF;

    I2C_AcknowledgeConfig(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFFFF;
    data = I2C_ReceiveData(I2C2) << 8;

    I2C_AcknowledgeConfig(I2C2, DISABLE);
    I2C_GenerateSTOP(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFFFF;
    data |= I2C_ReceiveData(I2C2);
    I2C_AcknowledgeConfig(I2C2, ENABLE);
    
    return data;
}

uint8_t MS5837_CalculateCRC(volatile MS5837_Calibration_t *cal)
{
    uint16_t n_prom[8] = {0};
    int cnt;
    uint16_t n_rem = 0;
    uint8_t n_bit;
    
    n_prom[0] = cal->C0;
    n_prom[1] = cal->C1;
    n_prom[2] = cal->C2;
    n_prom[3] = cal->C3;
    n_prom[4] = cal->C4;
    n_prom[5] = cal->C5;
    n_prom[6] = cal->C6;
    n_prom[7] = 0;
    
    n_prom[0] = n_prom[0] & 0x0FFF;
    
    for(cnt = 0; cnt < 16; cnt++) {
        if(cnt % 2 == 1) {
            n_rem ^= (uint16_t)((n_prom[cnt>>1]) & 0x00FF);
        } else {
            n_rem ^= (uint16_t)(n_prom[cnt>>1] >> 8);
        }
        
        for(n_bit = 8; n_bit > 0; n_bit--) {
            if(n_rem & 0x8000) {
                n_rem = (n_rem << 1) ^ 0x3000;
            } else {
                n_rem = (n_rem << 1);
            }
        }
    }
    
    n_rem = ((n_rem >> 12) & 0x000F);
    return (uint8_t)(n_rem ^ 0x00);
}

void MS5837_ReadCalibrationData(volatile MS5837_Calibration_t *cal)
{
    uint16_t raw[7] = {0};
    
    for(uint8_t i = 0; i < 7; i++) {
        raw[i] = MS5837_ReadPROM(CMD_MS5837_PROM_BASE + i*2);
    }
    
    cal->C0 = raw[0];
    cal->C1 = raw[1];
    cal->C2 = raw[2];
    cal->C3 = raw[3];
    cal->C4 = raw[4];
    cal->C5 = raw[5];
    cal->C6 = raw[6];
    
    uint8_t crc_read = (cal->C0 >> 12) & 0x0F;
    uint8_t crc_calculated = MS5837_CalculateCRC(cal);
    
    if(crc_read != crc_calculated) {
        printf("[MS5837]>>> CRC Error! Read: 0x%X, Calculated: 0x%X\r\n", crc_read, crc_calculated);
    }
    
    cal->C0 &= 0x0FFF;
}
void MS5837_StartTemperatureConversion(uint8_t osr)
{
    if (!I2C_WaitFlagTimeout(I2C2, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> StartTemp: Bus busy timeout!\r\n");
        return;
    }
    
    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return;
    
    I2C_SendData(I2C2, osr);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return;
    
    I2C_GenerateSTOP(I2C2, ENABLE);
}

void MS5837_StartPressureConversion(uint8_t osr)
{
    if (!I2C_WaitFlagTimeout(I2C2, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS))
    {
        printf("[MS5837]>>> StartPress: Bus busy timeout!\r\n");
        return;
    }
    
    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return;
    
    I2C_SendData(I2C2, osr);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return;
    
    I2C_GenerateSTOP(I2C2, ENABLE);
}

uint32_t MS5837_ReadADC(void)
{
    uint32_t data = 0;

    if (!I2C_WaitFlagTimeout(I2C2, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS))
    {
        return 0xFFFFFFFF; // 超时返回错误值
    }

    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    
    I2C_SendData(I2C2, CMD_MS5837_ADC_READ);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;

    I2C_GenerateSTART(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    
    I2C_Send7bitAddress(I2C2, MS5837_ADDR_READ, I2C_Direction_Receiver);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;

    I2C_AcknowledgeConfig(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    data = I2C_ReceiveData(I2C2) << 16;

    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    data |= I2C_ReceiveData(I2C2) << 8;

    I2C_AcknowledgeConfig(I2C2, DISABLE);
    I2C_GenerateSTOP(I2C2, ENABLE);
    if (!I2C_WaitEventTimeout(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFFFFFFFF;
    data |= I2C_ReceiveData(I2C2);
    I2C_AcknowledgeConfig(I2C2, ENABLE);
    
    return data;
}

// 内部浮点数计算函数
float MS5837_CalculateTemperature(uint32_t D2, volatile MS5837_Calibration_t *cal)
{
    int64_t dT;
    int32_t TEMP;
    int64_t Ti = 0;
    
    dT = (int64_t)D2 - ((int64_t)cal->C5 << 8);
    TEMP = 2000 + ((dT * (int64_t)cal->C6) >> 23);
    
    if(TEMP < 2000) {
        Ti = (3 * dT * dT) >> 33;
        TEMP = TEMP - (int32_t)Ti;
    } else {
        Ti = (2 * dT * dT) >> 37;
        TEMP = TEMP - (int32_t)Ti;
    }
    
    return (float)TEMP / 100.0f;  // 返回摄氏度
}

float MS5837_CalculatePressure(uint32_t D1, uint32_t D2, volatile MS5837_Calibration_t *cal)
{
    int64_t dT, OFF, SENS;
    int32_t TEMP;
    int64_t Ti = 0, OFFi = 0, SENSi = 0;
    
    dT = (int64_t)D2 - ((int64_t)cal->C5 << 8);
    TEMP = 2000 + ((dT * (int64_t)cal->C6) >> 23);
    
    OFF = ((int64_t)cal->C2 << 16) + ((dT * (int64_t)cal->C4) >> 7);
    SENS = ((int64_t)cal->C1 << 15) + ((dT * (int64_t)cal->C3) >> 8);
    
    if(TEMP < 2000) {
        Ti = (3 * dT * dT) >> 33;
        
        if(TEMP < -1500) {
            OFFi = (3 * (TEMP - 2000) * (TEMP - 2000)) >> 1;
            SENSi = (5 * (TEMP - 2000) * (TEMP - 2000)) >> 3;
            OFFi += 7 * (TEMP + 1500) * (TEMP + 1500);
            SENSi += 4 * (TEMP + 1500) * (TEMP + 1500);
        } else {
            OFFi = (3 * (TEMP - 2000) * (TEMP - 2000)) >> 1;
            SENSi = (5 * (TEMP - 2000) * (TEMP - 2000)) >> 3;
        }
    } else {
        Ti = (2 * dT * dT) >> 37;
        OFFi = (1 * (TEMP - 2000) * (TEMP - 2000)) >> 4;
        SENSi = 0;
    }
    
    OFF -= OFFi;
    SENS -= SENSi;
    TEMP -= (int32_t)Ti;
    
    int64_t P = (((int64_t)D1 * SENS) >> 21) - OFF;
    P = P >> 13;
    
    return (float)P * 0.1f;  // 返回mbar
}

float MS5837_CalculateDepth(float pressure_mbar)
{
    const float ATM = 1013.25f;
    float water = pressure_mbar - ATM;
    if(water < 0) water = 0;
    return water * 10.197f;  // 返回mm
}

//主要API函数实现

// 初始化MS5837传感器
int MS5837_Init(void)
{
    int ret = MS5837_OK;
    // 复位传感器
    MS5837_Reset();     // 实测：这里并不需要i2c发reset，后续可以直接过的！
    Delay_Ms(50);
    Delay_Us(100);

    // 读取校准系数并校验CRC
    MS5837_ReadCalibrationData(&g_calibration);

    // 检查CRC是否成功
    uint8_t crc_read = (g_calibration.C0 >> 12) & 0x0F;
    uint8_t crc_calculated = MS5837_CalculateCRC(&g_calibration);
    if(crc_read != crc_calculated) {ret = MS5837_ERR_CRC;}
    
    //Delay_Ms(10);

    printf("[MS5837]>>> Init SUCCESS!\r\n");
    printf("[MS5837]>>> PROM are: C1: %u, C2: %u, C3: %u, C4: %u, C5: %u, C6: %u\r\n",
           g_calibration.C1, g_calibration.C2, g_calibration.C3,
           g_calibration.C4, g_calibration.C5, g_calibration.C6);

    return ret;
}

// 读取传感器数据
int MS5837_Read(MS5837_Data_t *data)
{
    uint32_t D1, D2;
    
    // 启动温度转换（使用OSR=4096，平衡精度和速度）
    MS5837_StartTemperatureConversion(CMD_MS5837_CONVERT_D2_4096);
    Delay_Ms(10);  // 等待转换完成  —— OSR=4096时，最坏情况下MS5837转换耗时9.05ms，保守点给10ms delay
    
    // 读取温度ADC值
    D2 = MS5837_ReadADC();
    
    // 启动压力转换
    MS5837_StartPressureConversion(CMD_MS5837_CONVERT_D1_4096);
    Delay_Ms(10);  // 等待转换完成
    
    // 读取压力ADC值
    D1 = MS5837_ReadADC();
    
    // 计算浮点数结果
    float temperature_c = MS5837_CalculateTemperature(D2, &g_calibration);
    float pressure_mbar = MS5837_CalculatePressure(D1, D2, &g_calibration);
    float depth_mm = MS5837_CalculateDepth(pressure_mbar);
    
    // 转换为整数（直接截断小数部分）
    data->temperature_mdegC = (int32_t)(temperature_c * 1000.0f);  // 摄氏度转毫摄氏度
    data->pressure_mbar = (int32_t)pressure_mbar;                  // 直接取整数部分
    data->depth_mm = (int32_t)depth_mm;                            // 直接取整数部分
    
    return MS5837_OK;
}

/**
 * @brief I2C2 总线强制恢复函数（Bus Recovery）
 * @note  解决SCL/SDA死锁问题，符合I2C标准恢复流程：
 *        1. GPIO模拟时钟脉冲（最多9个）释放SDA
 *        2. 发送STOP条件
 *        3. 重新初始化I2C外设
 *        4. 实测可解决SCL线插拔导致的死锁
 */
void I2C2_BusRecovery(void)
{
    uint8_t i;
    uint16_t timeout;
    
    printf("[MS5837]>>> I2C2 Bus Recovery started...\r\n");
    
    /* 1. 禁用I2C2外设，释放引脚控制权 */
    I2C_Cmd(I2C2, DISABLE);
    I2C_DeInit(I2C2);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, DISABLE);
    
    /* 2. 使能GPIOB时钟（I2C2默认PB10-SCL, PB11-SDA） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    /* 3. 配置SCL和SDA为通用开漏输出模式 */
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;      // 开漏输出（可输入）
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;     // 高速输出
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    /* 4. 初始状态：先释放SDA和SCL（拉高） */
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);
    Delay_Us(10);  // 确保信号稳定
    
    /* 5. 生成最多9个SCL时钟脉冲，强制从设备释放SDA
     *    原理：从设备可能在等待时钟脉冲来完成当前字节传输
     *    每个脉冲：SCL低->高->低，检查SDA是否被释放（变高）
     */
    for (i = 0; i < 9; i++)
    {
        // SCL拉低
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        Delay_Us(5);
        
        // SCL拉高
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
        Delay_Us(5);
        
        // 检查SDA是否已释放（从设备停止拉低）
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == SET)
        {
            printf("[MS5837]>>> I2C2 Bus SDA released after %d clocks\r\n", i + 1);
            break;  // SDA已释放，提前退出
        }
    }
    
    /* 6. 发送STOP条件（即使SDA未释放也要尝试）
     *    STOP时序：SCL为高时，SDA从低变高
     *    先确保SDA为低，SCL为高，然后SDA变高
     */
    
    // 确保SDA为低电平
    GPIO_ResetBits(GPIOB, GPIO_Pin_11);
    Delay_Us(5);
    
    // SCL拉高
    GPIO_SetBits(GPIOB, GPIO_Pin_10);
    Delay_Us(5);
    
    // SDA从低变高，形成STOP条件
    GPIO_SetBits(GPIOB, GPIO_Pin_11);
    Delay_Us(10);  // STOP条件保持时间
    
    /* 7. 检查总线状态，确保BUSY标志被清除 */
    timeout = 1000;
    while (timeout--)
    {
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) == SET && 
            GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == SET)
        {
            break;  // 两根线都已拉高
        }
        Delay_Us(1);
    }
    
    /* 8. 重新初始化I2C2外设（恢复为复用开漏模式） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    
    // 重新配置GPIO为复用开漏（I2C模式）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;       // 复用开漏
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 调用你原有的初始化函数，或在此处重新配置
    IIC2_Init(400000);  // 400kHz
    
    /* 9. 验证恢复结果 */
    Delay_Ms(1);
    if (I2C_GetFlagStatus(I2C2, I2C_FLAG_BUSY) == RESET)
    {
        printf("[MS5837]>>> I2C2 Bus Recovery SUCCESS!\r\n");
    }
    else
    {
        printf("[MS5837]>>> I2C2 Bus Recovery FAILED! BUSY still set\r\n");
    }
}

// 串口printf打印数据，可做调试用
void MS5837_Print(void)
{
    //MS5837_Read(&sensor_data);
    
    // 处理温度：毫摄氏度 → 摄氏度（保留2位小数）
    int temp_mdegC = sensor_data.temperature_mdegC; // 毫摄氏度
    int temp_sign = (temp_mdegC < 0) ? -1 : 1;
    int temp_abs = (temp_mdegC < 0) ? -temp_mdegC : temp_mdegC;
    
    int temp_deg = temp_abs / 1000;        // 整数部分（摄氏度）
    int temp_cent = (temp_abs % 1000) / 10; // 小数部分（百分之一度，即2位小数）

    // 打印结果
    /*
    if (sensor_data.depth_mm>100000 || sensor_data.pressure_mbar>100000 || sensor_data.temperature_mdegC>100000 ) {
        printf("[Sensors]MS5837>>> Temp: -- degC, Press: -- mbar, Depth: -- mm\r\n");
    }else
    */
    if (temp_sign < 0) {
        printf("[MS5837]>>> Temp: -%d.%02d degC, Press: %d mbar, Depth: %d mm, %s\r\n",
               temp_deg, temp_cent,
               sensor_data.pressure_mbar,
               sensor_data.depth_mm,
               sensor_data.valid ? "VALID" : "INVALID");
    } else {
        printf("[MS5837]>>> Temp: %d.%02d degC, Press: %d mbar, Depth: %d mm, %s\r\n",
               temp_deg, temp_cent,
               sensor_data.pressure_mbar,
               sensor_data.depth_mm,
               sensor_data.valid ? "VALID" : "INVALID");
    }
}

float Get_Depth_Float_Meter(void)
{
    return sensor_data.depth_mm / 1000.0f;
}
