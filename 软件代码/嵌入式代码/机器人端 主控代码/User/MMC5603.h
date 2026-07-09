#ifndef __MMC5603_H
#define __MMC5603_H

#include "stdbool.h"

// 超时保护
//#define I2C_TIMEOUT_MS              500

/* MMC5603 I2C地址 (7-bit: 0x30) */
#define MMC5603_ADDR_WRITE          (0x30 << 1)       // 0x60
#define MMC5603_ADDR_READ           ((0x30 << 1) | 1) // 0x61

/* 关键寄存器地址 */
#define MMC5603_REG_XOUT0           0x00
#define MMC5603_REG_XOUT1           0x01
#define MMC5603_REG_YOUT0           0x02
#define MMC5603_REG_YOUT1           0x03
#define MMC5603_REG_ZOUT0           0x04
#define MMC5603_REG_ZOUT1           0x05
#define MMC5603_REG_XOUT2           0x06
#define MMC5603_REG_YOUT2           0x07
#define MMC5603_REG_ZOUT2           0x08
#define MMC5603_REG_STATUS1         0x18
#define MMC5603_REG_CTRL0           0x1B
#define MMC5603_REG_CTRL1           0x1C
#define MMC5603_REG_PRODUCT_ID      0x39

/* Control Register 0 位定义 */
#define MMC5603_CTRL0_TAKE_MEAS_M   (1 << 0)  // 启动磁场测量
#define MMC5603_CTRL0_AUTO_SR_EN    (1 << 5)  // 自动SET/RESET使能（必须置1）

/* Control Register 1 位定义 */
#define MMC5603_CTRL1_BW_6_6MS      0x00      // 6.6ms带宽（最高精度）

/* 20位模式参数 */
#define MMC5603_COUNTS_PER_GAUSS    16384.0f
#define MMC5603_ZERO_OFFSET_20BIT   524288    // 0x80000

/* 错误代码 */
#define MMC5603_OK                  0
#define MMC5603_ERR_ID              1

/* 磁场数据结构 */
typedef struct {
    int32_t x_raw, y_raw, z_raw;      // 传感器坐标系
    float   x_gauss, y_gauss, z_gauss;
    int32_t x_mgauss, y_mgauss, z_mgauss;
    int32_t total_mgauss;             // 合场强大小
    bool valid;                       // 是否有效
} MMC5603_Data_t;

extern MMC5603_Data_t mag_sensor_data;

/* 核心API */
int  MMC5603_Init(void);                      // 初始化：验证ID + 配置带宽
void MMC5603_StartMeasurement(void);          // 启动单次测量
void MMC5603_ReadData(MMC5603_Data_t *data);  // 读取数据
void MMC5603_Print(void);
float GetYawTrueNorth(void);                  // 返回真北方，deg，非磁北

#endif /* __MMC5603_H */
