/**
 * USB 触摸屏中继 - 头文件
 */

#ifndef __USB_TOUCH_RELAY_H
#define __USB_TOUCH_RELAY_H

#include "ch32v30x.h"

// USART3 初始化 (PB10=TX, PB11=RX)
void UART3_Init(uint32_t baudrate);

// 发送触摸数据包 (新格式: 6字节 0x99 header)
void Send_Touch_Packet(uint8_t tip_switch, uint16_t x, uint16_t y);

// 解析触摸屏 HID 报告
void Parse_Touch_Report(uint8_t *data, uint16_t len);

// 打印 USB 枚举状态（调试用）
void Print_USB_Status(void);

#endif
