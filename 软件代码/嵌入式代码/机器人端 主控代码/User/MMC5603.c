#include "debug.h"
#include "math.h"
#include "I2C.h"
#include "SysFlow.h"  // defined MAGNETIC_DECLINATION （磁偏角）20260303 update，约在本文件的279行用到
#include "MMC5603.h"

volatile u32 i2c1_tick_ms = 0;
MMC5603_Data_t mag_sensor_data = {0};

// 辅助函数：等待I2C事件带超时
// 实测：SDA线插拔可以恢复，SCL线插拔不可恢复。对于SCL线插拔，应该增加其他保护措施。
static uint8_t I2C_WaitEventTimeout(I2C_TypeDef* I2Cx, uint32_t event, uint32_t timeout_ms)
{
    uint32_t start_tick = i2c1_tick_ms;
    while (!I2C_CheckEvent(I2Cx, event))
    {
        if ((i2c1_tick_ms - start_tick) >= timeout_ms)
        {
            //I2C_ClearFlag(I2Cx, I2C_FLAG_AF);  // 必须加这行！
            //I2C_DeInit(I2C1);
            //RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, DISABLE);
            //IIC_Init(400000);

            //I2C_Send7bitAddress(I2C1, 0X00, I2C_Direction_Transmitter);
            //I2C_GenerateSTOP(I2C1, ENABLE);
            IIC1_Recovery(400000);

            return 0; // 超时
        }
    }
    return 1; // 成功
}

// 辅助函数：等待I2C标志带超时
static uint8_t I2C_WaitFlagTimeout(I2C_TypeDef* I2Cx, uint32_t flag, FlagStatus status, uint32_t timeout_ms)
{
    uint32_t start_tick = i2c1_tick_ms;
    while (I2C_GetFlagStatus(I2Cx, flag) == status)
    {
        if ((i2c1_tick_ms - start_tick) >= timeout_ms)
        {
            //I2C_Send7bitAddress(I2C1, 0X00, I2C_Direction_Transmitter);
            //I2C_GenerateSTOP(I2C1, ENABLE);
            IIC1_Recovery(400000);

            return 0; // 超时
        }
    }
    return 1; // 成功
}

/**
 * I2C写单字节（完全复用MS5837的写操作逻辑）
 * 注意：写操作中ADDR标志由硬件自动清除，无需手动处理
 */
static void MMC5603_I2C_WriteByte(uint8_t reg_addr, uint8_t data)
{
    // 1. 等待总线空闲
    if(!I2C_WaitFlagTimeout(I2C1, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS)) return;
    // 2. 生成START
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return; // EV5
    
    // 3. 发送器件地址+写
    I2C_Send7bitAddress(I2C1, MMC5603_ADDR_WRITE, I2C_Direction_Transmitter);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return; // EV6
    
    // 4. 发送寄存器地址
    I2C_SendData(I2C1, reg_addr);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return; // EV8
    
    // 5. 发送数据
    I2C_SendData(I2C1, data);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return; // EV8
    
    // 6. 生成STOP
    I2C_GenerateSTOP(I2C1, ENABLE);
}

/**
 * I2C读单字节（关键修复：Repeated START后必须清除ADDR标志）
 * 严格遵循数据手册Page 12-13的测量流程
 */
static uint8_t MMC5603_I2C_ReadByte(uint8_t reg_addr)
{
    uint8_t data = 0;
    
    // 第1阶段：发送寄存器地址（写操作）
    if(!I2C_WaitFlagTimeout(I2C1, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS)) return 0xFF;
    
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFF; // EV5
    
    I2C_Send7bitAddress(I2C1, MMC5603_ADDR_WRITE, I2C_Direction_Transmitter);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFF; // EV6
    
    I2C_SendData(I2C1, reg_addr);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return 0xFF; // EV8
    
    // 第2阶段：Repeated START + 读数据
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return 0xFF; // EV5
    
    I2C_Send7bitAddress(I2C1, MMC5603_ADDR_READ, I2C_Direction_Receiver);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, I2C_TIMEOUT_MS)) return 0xFF; // EV6
    
    // === 关键修复点 ===
    // CH32V307要求：在EV6后、读DR前必须清除ADDR标志
    // 顺序：先读STAR1，再读STAR2（缺一不可）
    // 清 ADDR 标志（读 SR1 再读 SR2）
    //  + 在读取前置位ACK
    //I2C_AcknowledgeConfig(I2C1, ENABLE);
    (void)I2C1->STAR1;
    (void)I2C1->STAR2;
    // =================
    
    // 单字节读取：提前禁用ACK + 生成STOP
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);
    
    // 等待数据接收
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return 0xFF; // EV7
    data = I2C_ReceiveData(I2C1);
    
    // 恢复ACK配置
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    
    return data;
}

/**
 * I2C读9字节（关键修复：Repeated START后清除ADDR标志）
 */
static void MMC5603_I2C_Read9Bytes(uint8_t *buf)
{
    uint8_t i;
    
    // 第1阶段：发送寄存器地址0x00（XOUT0）
    if(!I2C_WaitFlagTimeout(I2C1, I2C_FLAG_BUSY, SET, I2C_TIMEOUT_MS)) return;
    
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return; // EV5
    
    I2C_Send7bitAddress(I2C1, MMC5603_ADDR_WRITE, I2C_Direction_Transmitter);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, I2C_TIMEOUT_MS)) return; // EV6
    
    I2C_SendData(I2C1, MMC5603_REG_XOUT0);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED, I2C_TIMEOUT_MS)) return; // EV8
    
    // 第2阶段：Repeated START + 读9字节
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_MODE_SELECT, I2C_TIMEOUT_MS)) return; // EV5
    
    I2C_Send7bitAddress(I2C1, MMC5603_ADDR_READ, I2C_Direction_Receiver);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, I2C_TIMEOUT_MS)) return; // EV6
    
    // 清 ADDR 标志（读 SR1 再读 SR2） + 在读取前置位ACK
    //I2C_AcknowledgeConfig(I2C1, ENABLE);
    (void)I2C1->STAR1;
    (void)I2C1->STAR2;

    // 读取前8字节（带ACK）
    for(i = 0; i < 8; i++)
    {
        if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return; // EV7
        buf[i] = I2C_ReceiveData(I2C1);
    }
    
    // 读取第9字节（带NACK+STOP）
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);
    if(!I2C_WaitEventTimeout(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED, I2C_TIMEOUT_MS)) return; // EV7
    buf[8] = I2C_ReceiveData(I2C1);
    
    // 恢复ACK配置
    I2C_AcknowledgeConfig(I2C1, ENABLE);
}

/**
 * 初始化：严格遵循上电时序（Page 15）
 */
int MMC5603_Init(void)
{
    uint8_t id = 0;
    
    // 首次通信要先读Product ID（0x39）
    id = MMC5603_I2C_ReadByte(MMC5603_REG_PRODUCT_ID);
    printf("[MMC5603]>>> Product ID = 0x%02X\r\n", id);
    
    if(id != 0x10)
    {
        printf("[MMC5603]>>> ID mismatch! Expected 0x10\r\n");
        //return -1;
    }
    
    // 配置带宽为6.6ms (BW=00)
    MMC5603_I2C_WriteByte(MMC5603_REG_CTRL1, MMC5603_CTRL1_BW_6_6MS);
    printf("[MMC5603]>>> CTRL1 set to 0x00 (6.6ms BW)\r\n");
    
    // 清除Status1寄存器（读一次即可）
    id = MMC5603_I2C_ReadByte(MMC5603_REG_STATUS1);
    printf("[MMC5603]>>> Status1 cleared (value=0x%02X)\r\n", id);
    
    printf("[MMC5603]>>> Init SUCCESS!\r\n");
    return 0;
}

/**
 * 启动单次磁场测量（Page 12-13）
 * 写CTRL0 = 0x21 (Auto_SR_en | Take_meas_M)
 */
void MMC5603_StartMeasurement(void)
{
    MMC5603_I2C_WriteByte(MMC5603_REG_CTRL0, 
                         MMC5603_CTRL0_AUTO_SR_EN | MMC5603_CTRL0_TAKE_MEAS_M);
}

/**
 * 读取并处理磁场数据
 */
void MMC5603_ReadData(MMC5603_Data_t *data)
{
    uint8_t buf[9] = {0};
    uint32_t raw_x, raw_y, raw_z;
    
    // 1. 读取9字节原始数据（0x00-0x08）
    MMC5603_I2C_Read9Bytes(buf);
    
    // 2. 拼接20位值（Page 7-8）
    raw_x = ((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | ((uint32_t)buf[6] >> 4);
    raw_y = ((uint32_t)buf[2] << 12) | ((uint32_t)buf[3] << 4) | ((uint32_t)buf[7] >> 4);
    raw_z = ((uint32_t)buf[4] << 12) | ((uint32_t)buf[5] << 4) | ((uint32_t)buf[8] >> 4);
    // 【关键调试】打印原始 9 字节十六进制值   和上述拼接后的raw xyz 值。
    // printf("Buf: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
    // printf("raw x: %d, raw y: %d, raw z: %d.\r\n",raw_x,raw_y,raw_z);
    
    // 3. 零点校正（20位模式：524288 = 0x80000 对应0 Gauss）
    data->x_raw = (int32_t)raw_x - MMC5603_ZERO_OFFSET_20BIT;
    data->y_raw = (int32_t)raw_y - MMC5603_ZERO_OFFSET_20BIT;
    data->z_raw = (int32_t)raw_z - MMC5603_ZERO_OFFSET_20BIT;
    
    // 4. 转换为物理单位
    data->x_gauss = (float)data->x_raw / MMC5603_COUNTS_PER_GAUSS;
    data->y_gauss = (float)data->y_raw / MMC5603_COUNTS_PER_GAUSS;
    data->z_gauss = (float)data->z_raw / MMC5603_COUNTS_PER_GAUSS;
    
    data->x_mgauss = (int32_t)(data->x_gauss * 1000.0f);
    data->y_mgauss = (int32_t)(data->y_gauss * 1000.0f);
    data->z_mgauss = (int32_t)(data->z_gauss * 1000.0f);
}

/**
 * 计算航向角（水平状态，无需IMU补偿）
 * @param mag_x_mG 模块X轴磁场（mG），+X指向西
 * @param mag_y_mG 模块Y轴磁场（mG），+Y指向南
 * @return 真北航向角（0~360°）
 */
float Calculate_Heading_Angle(float mag_x_mG, float mag_y_mG)
{
    float heading_rad, heading_deg;

    // 2026/02/26晚上：
    heading_rad = atan2f(-mag_x_mG, mag_y_mG);
    /*
    // 2026/02/12 晚上之前的代码：
    // 1. 计算磁北航向（atan2返回-π~+π）
    // -mag_x_mG → 地理东向分量
    // -mag_y_mG → 地理北向分量
    //heading_rad = atan2f(-mag_x_mG, -mag_y_mG);
    */
    /* 
    // 2026/02/12 晚上的代码：
    // 1. 计算标准航向角：从正北（Y轴）顺时针计算
    // atan2(X, Y) 给出从正北顺时针的角度
    heading_rad = atan2f(mag_x_mG, mag_y_mG);
    */
    
    // 2. 转换为0~360°范围
    heading_deg = heading_rad * 180.0f / M_PI;
    heading_deg+=90;  // 20260226晚上加的，怀疑就是三轴方向问题
    if (heading_deg < 0.0f) {
        heading_deg += 360.0f;
    }
    
    // 3. 磁偏角修正（自贡市≈-3.2°，磁北偏西）
    // const float MAGNETIC_DECLINATION = -3.2f;  // 单位：度
    // 20260303 note: MAGNETIC_DECLINATION 已经在 sysflow.h 定义了
    heading_deg -= MAGNETIC_DECLINATION;       // 真北 = 磁北 - 磁偏角
    
    // 4. 确保结果在0~360°范围内
    if (heading_deg >= 360.0f) {
        heading_deg -= 360.0f;
    } else if (heading_deg < 0.0f) {
        heading_deg += 360.0f;
    }
    
    return heading_deg;
}

/**
 * 打印磁场数据
 */
void MMC5603_Print(void)
{
    float heading = Calculate_Heading_Angle(mag_sensor_data.x_gauss/1000, mag_sensor_data.y_gauss/1000);
    printf("[MMC5603]>>> MagX:%d mGs, MagY:%d mGs, MagZ:%d mGs, Total:%d mGs, Heading:%d.%02d deg, %s\r\n",
           mag_sensor_data.x_mgauss, 
           mag_sensor_data.y_mgauss, 
           mag_sensor_data.z_mgauss,
           mag_sensor_data.total_mgauss,
           (u32)heading,
           (u32)(heading*100) % 100,
           mag_sensor_data.valid ? "VALID" : "INVALID");
}

// 返回真北方，deg，非磁北
float GetYawTrueNorth(void)
{
    return Calculate_Heading_Angle(mag_sensor_data.x_gauss/1000, mag_sensor_data.y_gauss/1000);
}