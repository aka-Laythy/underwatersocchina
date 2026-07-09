#ifndef __SIM_H
#define __SIM_H

#include "ch32v30x.h"

#define SIM_RX_BUF_SIZE 512   // 接收缓冲区（足够容纳多行响应）
#define SIM_TX_BUF_SIZE 512   // 发送缓冲区

// 初始化函数
void SIM_Init(void);
// 非阻塞发送AT指令（自动添加\r\n）
void SIM_SendCmd(const char *cmd);
// 行解析接口（推荐用于AT指令响应处理
uint16_t SIM_GetLine(uint8_t *buf, uint16_t max_len);

#endif