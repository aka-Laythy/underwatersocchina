#ifndef __FIBER_H
#define __FIBER_H

#include <stdint.h>

// 遥控包头：
// 因为光纤模块只有一个串口，linux的数据和mcu的数据都靠这个串口进行双向通信，必须严格区分（按包头）
// 现在写了mcu的，后面linux要插进来传输的话，要判断uart空闲才能发！不然就出大问题！
// 光纤模块uart速度是400kbps，保守给375kbps，而我的mcu只需要占用其中6.4kbps（ <1.8% ）
//
// 计算：8N1, 字节：6(数据)0+1+1（起始+停止位）=64bit，间隔10ms发一帧，speed =64b/0.01s=6.4kbps。
#define REMOTECONTROL_PACKET_HEADER 0x66  // dec 102，遥控姿态数据
#define REMOTEDATA_PACKET_HEADER    0x88  // dec 136，遥控其他数据
// 包头只要比 x y z yaw checksum 都大就行，when input x,y,z,yaw in [-50,50]，output checksum in [0,60]，所以包头最小选61

// 遥控包结构
// 实际并未使用，只是给人看。
typedef struct {
    int8_t header;      // should be REMOTECONTROL_PACKET_HEADER 0x66
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t yaw;
    int8_t checksum;
} __attribute__((packed)) RemoteControl_Packet;

typedef struct {
    int8_t header;      // should be 88
    int8_t pending;     // 待定
    int8_t checksum;
} __attribute__((packed)) RemoteData_Packet;

// 配置参数
#define BAUD_RATE       375000
#define TIMEOUT_MS      100
#define MAX_ERROR_COUNT 5

// 全局滴答（由TIM6中断更新）
extern volatile uint32_t fiber_ticks_ms ;
// 超时使能标志
extern volatile uint8_t timeout_enabled ;
// 接收状态
extern volatile uint8_t rx_buffer[6];
extern volatile uint8_t rx_index;
extern volatile uint8_t error_count;
extern volatile uint8_t valid_frame_received;

void Fiber_Init(void);
void Fiber_IRQHandler(void);      // UART4 中断
void Fiber_Process(void);         // 主循环调用

// 调试用：enable/disenable超时控制
void Fiber_EnableTimeout(void);
void Fiber_DisableTimeout(void);
int  Fiber_IsTimeoutEnabled(void);
void Fiber_Debug_Print(void);

#endif /* __FIBER_H */
