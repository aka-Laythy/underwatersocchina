#ifndef __FIBER_H
#define __FIBER_H

#define REMOTECONTROL_PACKET_HEADER 0x66

typedef struct {
    int8_t header;      // should be REMOTECONTROL_PACKET_HEADER 0x66
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t yaw;
    int8_t checksum;
} __attribute__((packed)) RemoteControl_Packet;

// 初始化 USART3 (PB10-TX, PB11-RX)，开启中断收发
void Fiber_Init(void);

/**
 * @brief  中断方式发送一帧遥控数据
 * @param  x, y, z, yaw: 控制量（int8_t）
 * @retval 0: 成功启动发送  1: 发送忙（上一帧未发完）
 * @note   校验和自动计算：只取 x,y,z,yaw 的低4位相加
 */
u8 Fiber_Send_RemoteControl(int8_t x, int8_t y, int8_t z, int8_t yaw);

#endif /* __MAIN_H */