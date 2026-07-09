#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "stdint.h"

// 初始化和基本操作
void SPI2_Init(void);
uint8_t SPI2_ReadWriteByte(uint8_t byte);
void W25Q512_CS_Ctrl(uint8_t level);
void W25Q512_ReadJEDEC_ID(void);
uint8_t W25Q512_VerifyChip(void);

// 读写数据接口

#endif /* __SPI_FLASH_H */
