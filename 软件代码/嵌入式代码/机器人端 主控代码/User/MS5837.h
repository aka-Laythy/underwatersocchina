#ifndef __MS5837_H
#define __MS5837_H

#include <stdbool.h>

// I2C 超时设置（单位：毫秒）
//#define I2C_TIMEOUT_MS           5      // 5ms超时足够（400kHz I2C 最大传输时间 < 1ms）
extern volatile uint32_t i2c2_tick_ms;     // 1ms心跳计数器

// 错误代码定义
#define MS5837_OK          0
#define MS5837_ERR_CRC     1
#define MS5837_ERR_I2C     2
#define MS5837_ERR_TIMEOUT 3

// MS5837-30BA I2C Address
#define MS5837_I2C_ADDR             0x76
//#define MS5837_ADDR_WRITE           0xEC
//#define MS5837_ADDR_READ            0xED
#define MS5837_ADDR_WRITE           (MS5837_I2C_ADDR << 1)       // 0xEC
#define MS5837_ADDR_READ            ((MS5837_I2C_ADDR << 1) | 1) // 0xED

// MS5837-30BA Commands
#define CMD_MS5837_RESET            0x1E
#define CMD_MS5837_ADC_READ         0x00
#define CMD_MS5837_PROM_BASE        0xA0  // 基地址，C0=0xA0, C1=0xA2, ..., C6=0xAC

// Pressure Conversion Commands
#define CMD_MS5837_CONVERT_D1_256   0x40
#define CMD_MS5837_CONVERT_D1_512   0x42
#define CMD_MS5837_CONVERT_D1_1024  0x44
#define CMD_MS5837_CONVERT_D1_2048  0x46
#define CMD_MS5837_CONVERT_D1_4096  0x48  // 推荐使用
#define CMD_MS5837_CONVERT_D1_8192  0x4A

// Temperature Conversion Commands
#define CMD_MS5837_CONVERT_D2_256   0x50
#define CMD_MS5837_CONVERT_D2_512   0x52
#define CMD_MS5837_CONVERT_D2_1024  0x54
#define CMD_MS5837_CONVERT_D2_2048  0x56
#define CMD_MS5837_CONVERT_D2_4096  0x58  // 推荐使用
#define CMD_MS5837_CONVERT_D2_8192  0x5A

// 测量结果结构体（整数类型）
typedef struct
{
    int32_t temperature_mdegC;  // 温度，单位：毫摄氏度 (1°C = 1000 mdegC)
    int32_t pressure_mbar;      // 压力，单位：毫巴 (mbar)，整数部分
    int32_t depth_mm;           // 水深，单位：毫米 (mm)，整数部分
    bool valid;                 // 是否有效
} MS5837_Data_t;

// 校准系数结构体
typedef struct
{
    uint16_t C0;  // 高4位: CRC, 低12位: 工厂保留
    uint16_t C1;  // 压力灵敏度系数 (SENS_T1)
    uint16_t C2;  // 压力偏移系数 (OFF_T1)
    uint16_t C3;  // 温度系数压力灵敏度 (TCS)
    uint16_t C4;  // 温度系数压力偏移 (TCO)
    uint16_t C5;  // 参考温度 (T_REF)
    uint16_t C6;  // 温度灵敏度系数 (TEMPSENS)
} MS5837_Calibration_t;

// 校正系数存储变量
extern volatile MS5837_Calibration_t g_calibration;

// 对外数据接口变量
extern MS5837_Data_t sensor_data;

// --------------------- 主要API函数（用户只需调用这两个）
// @brief 初始化MS5837传感器 @param bound I2C时钟频率(单位: Hz) @return 0: 成功, 其他: 错误码
int MS5837_Init(void);
// @brief 读取传感器数据（温度、压力、水深） @param data 存储测量结果的结构体指针 @return 0: 成功, 其他: 错误码
int MS5837_Read(MS5837_Data_t *data);
// 串口printf打印数据，可做调试用
void MS5837_Print(void);

// --------------------- 以下是MS5837的内部函数，在此引出，仅为调试用！
// 强制释放I2C2总线
void I2C2_BusRecovery(void);
// MS5837基本操作
void MS5837_Reset(void);
uint16_t MS5837_ReadPROM(uint8_t prom_addr);
uint32_t MS5837_ReadADC(void);
void MS5837_StartTemperatureConversion(uint8_t osr);
void MS5837_StartPressureConversion(uint8_t osr);
// 校准数据读取和校验
void MS5837_ReadCalibrationData(volatile MS5837_Calibration_t *cal);
uint8_t MS5837_CalculateCRC(volatile MS5837_Calibration_t *cal);
// 浮点数计算函数
float MS5837_CalculateTemperature(uint32_t D2, volatile MS5837_Calibration_t *cal);
float MS5837_CalculatePressure(uint32_t D1, uint32_t D2, volatile MS5837_Calibration_t *cal);
float MS5837_CalculateDepth(float pressure_mbar);
// 读取当前深度（米）
float Get_Depth_Float_Meter(void);

#endif  /* __SENSORS_H */
