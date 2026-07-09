#ifndef __I2C_H
#define __I2C_H

#include "ch32v30x.h"
#include <stdint.h>

// I2C1: MMC5603 (磁力计)
// I2C2: MS5837  (深度计)

// Host I2C Address (master mode)
#define HOST_ADDRESS     0x10

// 超时保护
#define I2C_TIMEOUT_MS   5

void IIC2_Scan_Soft(void);
void IIC2_Init(uint32_t bound);
void IIC2_Recovery(uint32_t bound);
void IIC2_Scan(void);

void IIC1_Init(uint32_t bound);
void IIC1_Recovery(uint32_t bound);
void IIC1_Scan(void);

#endif /* __I2C_H */
