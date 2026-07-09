#ifndef __LINUXCONNECT_H
#define __LINUXCONNECT_H

/* 包头定义 */
#define LINUX_3D_PACKET_HEADER      0x77  // dec 119，与linux板通信数据 - 3D回传包
#define LINUX_TASK_PACKET_HEADER    0x78  // dec 120，与linux板通信数据 - 任务下发包
#define LINUX_4POINTS_PACKET_HEADER 0x79  // dec 121，与linux板通信数据 - 避障包

/* 包总长度（含header, data, crc8，不含valid） */
#define PACKET_3D_SIZE      140     // 0x77包：1+138+1
#define PACKET_TASK_SIZE    3331    // 0x78包：1+1+(1+4+4+4)*256+1
#define PACKET_4POINTS_SIZE 14      // 0x79包：1+3+3+3+3+1

typedef enum {
    STATE_WAIT_HEADER = 0,      // 等待包头
    STATE_RECV_3D,              // 接收3D包中（固定140字节）
    STATE_RECV_TASK_POINTNUM,   // 已收0x78，等待接收point_num（第2字节）
    STATE_RECV_TASK_PAYLOAD     // 已收point_num，接收航点数据+crc8
} RecvState_t;

/* 3D包 */
typedef struct {
    u8 header;          // 0x77
    u8 data[46][3];     // 46组，每组3个ASCII字符（如 "123"）
    u8 crc8;            // CRC8
    u8 valid;           // 1 for valid
} __attribute__((packed)) Linux_3D_Packet;

/* task点包，packed对齐，串口接收层 */
typedef struct {
    u8 num;         // 航点编号
    float lon;      // 经度，.6f精度
    float lat;      // 纬度，.6f精度  
    float depth;    // 深度，.2f精度
} __attribute__((packed)) Waypoint;
typedef struct {
    u8 header;              // should be 0x78
    u8 point_num;           // 0~256
    Waypoint points[256];   // 航点数组，预留256个
    u8 crc8;                // CRC8
    u8 valid;               // 1 for valid
} __attribute__((packed)) Linux_Task_Packet;

/* task点包，自然对齐版，用于业务逻辑 */
// 注意：无packed属性，编译器自动4字节对齐，float访问安全
typedef struct {
    u8 num;
    float lon;
    float lat;
    float depth;
} Waypoint_Application;  // 大小可能是16字节（含填充），但访问安全
typedef struct {
    u8 header;
    u8 point_num;
    Waypoint_Application points[256];  // 自然对齐，可安全访问float
    u8 crc8;
    u8 valid;
} Linux_Task_Application_Packet;

/* 接收缓冲（使用Union节省内存） */
typedef union {
    Linux_3D_Packet pkt_3d;
    Linux_Task_Packet pkt_task;
    uint8_t raw[PACKET_TASK_SIZE];  // 最大包大小
} PacketBuffer_t;

// 配置参数
#define BAUD_RATE_LINUX   115200
#define TIMEOUT_MS        100

/* 接口函数 */
void LinuxConnect_Init(void);
void LinuxConnect_CheckTimeout(void);  // 需每ms或主循环调用，检查超时
/* 获取接收完成的包（非阻塞查询） */
u8 LinuxConnect_Get3DPacket(Linux_3D_Packet* out);
u8 LinuxConnect_GetTaskPacket(Linux_Task_Application_Packet* out);  //返回自然对齐版本，内部自动解包
/* CRC8计算（备用） */
uint8_t CRC8_Calculate(const uint8_t* data, uint16_t len);

#endif /* __LINUXCONNECT_H */
