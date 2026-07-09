#include "ch32v30x.h"
#include <string.h>
#include "math.h"

#include "LinuxConnect.h"

/* 外部滴答计数器（需在主循环/SysTick中递增） */
extern volatile u32 tick;

/* 接收状态机静态变量 */
static RecvState_t recv_state = STATE_WAIT_HEADER;
static PacketBuffer_t rx_buffer;
static uint16_t recv_index = 0;
static uint16_t target_len = 0;      // 本次需要接收的总字节数
static uint32_t last_rx_tick = 0;

/* 接收完成标志 */
static volatile u8 flag_3d_ready = 0;
static volatile u8 flag_task_ready = 0;
static uint8_t g_task_crc8 = 0;            // 暂存 CRC8，解决结构体偏移问题

/* 临时存储区（双缓冲，供主循环取走） */
static Linux_3D_Packet ready_3d_pkt;
static Linux_Task_Packet ready_task_pkt;

/* 应用层调用区，直接extern */
Linux_Task_Application_Packet task_application_pkt;

/**
 * @brief CRC8-MAXIM计算（多项式0x31，初始值0xFF）
 */
uint8_t CRC8_Calculate(const uint8_t* data, uint16_t len)
{
    uint8_t crc = 0xFF;
    while(len--)
    {
        crc ^= *data++;
        for(uint8_t i = 0; i < 8; i++)
        {
            if(crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief 重置接收状态机
 */
static void ResetReceiver(void)
{
    recv_state = STATE_WAIT_HEADER;
    recv_index = 0;
    target_len = 0;
    // 可选：清零缓冲区，防止旧数据干扰（会消耗时间，可选）
    // memset(&rx_buffer, 0, sizeof(rx_buffer));
}

/**
 * @brief 完成接收处理（在中断末尾调用）
 */
static void FinishReceive(void)
{
    if(recv_state == STATE_RECV_3D)
    {
        memcpy(&ready_3d_pkt, &rx_buffer.pkt_3d, sizeof(Linux_3D_Packet));
        ready_3d_pkt.valid = 1;
        flag_3d_ready = 1;
    }
    else if(recv_state == STATE_RECV_TASK_PAYLOAD)
    {
        // 复制整个结构体（注意：rx_buffer中只有前target_len字节是刚接收的，后面是旧数据）
        // 但由于 valid 标记和 point_num 字段正确，业务代码只会访问前point_num个航点
        //memcpy(&ready_task_pkt, &rx_buffer.pkt_task, sizeof(Linux_Task_Packet));
        // 260303 night 关键修复 1：只拷贝实际接收到的长度，不要拷贝整个 3332 字节！
        memcpy(&ready_task_pkt, &rx_buffer.pkt_task, target_len);
        g_task_crc8 = rx_buffer.raw[target_len - 1];
        
        ready_task_pkt.valid = 1;
        flag_task_ready = 1;
    }
    ResetReceiver();
}

void LinuxConnect_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3 | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    GPIO_PinRemapConfig(GPIO_FullRemap_USART3, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_8;     // USART3_TX_3
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &gpio);
    
    gpio.GPIO_Pin = GPIO_Pin_9;     // USART3_RX_3
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &gpio);

    USART_InitTypeDef usart;
    usart.USART_BaudRate = BAUD_RATE_LINUX;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &usart);

    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART3, ENABLE);
    
    printf("[Linux Connect]>>> Init SUCCESS!\r\n");
}

// USART3 中断服务函数
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void)
{
    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        uint8_t byte = USART_ReceiveData(USART3);
        last_rx_tick = tick;
        
        switch(recv_state)
        {
            case STATE_WAIT_HEADER:
            {
                if(byte == LINUX_3D_PACKET_HEADER)
                {
                    // 收到0x77，准备接收140字节固定长度
                    target_len = PACKET_3D_SIZE;
                    recv_state = STATE_RECV_3D;
                    recv_index = 0;
                    rx_buffer.raw[recv_index++] = byte;
                }
                else if(byte == LINUX_TASK_PACKET_HEADER)
                {
                    // 收到0x78，先收2字节（header + point_num）
                    target_len = 2;
                    recv_state = STATE_RECV_TASK_POINTNUM;
                    recv_index = 0;
                    rx_buffer.raw[recv_index++] = byte;
                }
                // 其他字节丢弃
                break;
            }
            
            case STATE_RECV_3D:
            {
                // 重同步检查：如果中途收到新包头，丢弃当前包
                if(byte == LINUX_3D_PACKET_HEADER || byte == LINUX_TASK_PACKET_HEADER)
                {
                    ResetReceiver();
                    // 重新处理这个字节作为新包头
                    if(byte == LINUX_3D_PACKET_HEADER)
                    {
                        target_len = PACKET_3D_SIZE;
                        recv_state = STATE_RECV_3D;
                        rx_buffer.raw[0] = byte;
                        recv_index = 1;
                    }
                    else // 0x78
                    {
                        target_len = 2;
                        recv_state = STATE_RECV_TASK_POINTNUM;
                        rx_buffer.raw[0] = byte;
                        recv_index = 1;
                    }
                    break;
                }
                
                // 正常接收
                if(recv_index < target_len)
                {
                    rx_buffer.raw[recv_index++] = byte;
                    if(recv_index >= target_len)
                    {
                        FinishReceive();
                    }
                }
                break;
            }
            
            case STATE_RECV_TASK_POINTNUM:
            {
                // 接收第2个字节（point_num）
                rx_buffer.raw[recv_index++] = byte;
                
                if(recv_index >= target_len) // 已收满2字节
                {
                    uint8_t point_num = rx_buffer.pkt_task.point_num;
                    
                    // 计算剩余需要接收的长度：航点数据 + crc8
                    // 总长度 = 2(header+point_num) + point_num*13 + 1(crc8)
                    if(point_num <= 255)
                    {
                        uint16_t payload_len = (uint16_t)point_num * sizeof(Waypoint) + 1; // +1 for crc8
                        target_len = 2 + payload_len;
                        
                        // 安全检查：不能超过缓冲区上限
                        if(target_len > PACKET_TASK_SIZE)
                        {
                            ResetReceiver(); // 非法长度，重置
                        }
                        else
                        {
                            recv_state = STATE_RECV_TASK_PAYLOAD;
                        }
                    }
                    else
                    {
                        ResetReceiver(); // point_num 超出范围（理论上不会，因为u8最大255）
                    }
                }
                break;
            }
            
            case STATE_RECV_TASK_PAYLOAD:
            {
                /*
                // 删除re-sync！（重同步），因为是二进制（而非ascii）发送，0x78不再是“魔数”无法唯一确定包头
                // 如果启用re-sync，几乎100%会出错（数据中含0x78）
                // 重同步检查
                if(byte == LINUX_3D_PACKET_HEADER || byte == LINUX_TASK_PACKET_HEADER)
                {
                    ResetReceiver();
                    if(byte == LINUX_3D_PACKET_HEADER)
                    {
                        target_len = PACKET_3D_SIZE;
                        recv_state = STATE_RECV_3D;
                        rx_buffer.raw[0] = byte;
                        recv_index = 1;
                    }
                    else // 0x78
                    {
                        target_len = 2;
                        recv_state = STATE_RECV_TASK_POINTNUM;
                        rx_buffer.raw[0] = byte;
                        recv_index = 1;
                    }
                    break;
                }
                */
                
                // 正常接收数据
                if(recv_index < target_len)
                {
                    rx_buffer.raw[recv_index++] = byte;
                    
                    if(recv_index >= target_len)
                    {
                        /*
                        // 先不开启crc8校验
                        uint8_t calc_crc = CRC8_Calculate(rx_buffer.raw, target_len - 1);
                        uint8_t recv_crc = rx_buffer.raw[target_len - 1];
                        if(calc_crc == recv_crc)
                        {
                            FinishReceive();  // 只有 CRC 正确才提交给应用层
                        }
                        else
                        {
                            // CRC 错误，丢弃整包
                            ResetReceiver();  
                        }
                        */
                        FinishReceive();
                    }
                }
                break;
            }
            
            default:
                ResetReceiver();
                break;
        }
    }
    
    
    /*
    // 不使用：可能重复读DR，导致数据错位
    // 清除过载错误标志
    if(USART_GetITStatus(USART3, USART_IT_ORE) != RESET)
    {
        USART_ReceiveData(USART3); // 清ORE标志
    }
    */
}

/**
 * @brief 超时检查函数（建议在主循环每1ms调用）
 */
void LinuxConnect_CheckTimeout(void)
{
    if(recv_state != STATE_WAIT_HEADER)
    {
        if((tick - last_rx_tick) > TIMEOUT_MS)
        {
            ResetReceiver();
        }
    }
}

/**
 * @brief 获取3D包（非阻塞）
 * @return 1:有新包 0:无新包
 */
u8 LinuxConnect_Get3DPacket(Linux_3D_Packet* out)
{
    if(flag_3d_ready)
    {
        memcpy(out, &ready_3d_pkt, sizeof(Linux_3D_Packet));
        flag_3d_ready = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief 获取Task包（自动解包到自然对齐结构体）
 * @param out 自然对齐的结构体指针，可安全访问float
 * @return 1:有新包 0:无新包
 * 
 * 注意：此函数内部使用memcpy从packed缓冲区解包float，避免RISC-V非对齐访问错误
 */
u8 LinuxConnect_GetTaskPacket(Linux_Task_Application_Packet* out)
{
    if(flag_task_ready)
    {
        out->header = ready_task_pkt.header;
        out->point_num = ready_task_pkt.point_num;
        out->crc8 = g_task_crc8;  // 使用暂存的 CRC，而不是结构体中的字段
        out->valid = 1;
        
        // 关键修复 3：手动解包 float，避免编译器 packed 属性失效导致的错位
        // 通过字节偏移手动读取，确保无论编译器如何对齐都能正确解析
        uint8_t* src = (uint8_t*)ready_task_pkt.points; // 指向第一个航点
        
        for(int i = 0; i < ready_task_pkt.point_num; i++)
        {
            out->points[i].num = src[0];
            memcpy(&out->points[i].lon, src + 1, 4);   // offset 1
            memcpy(&out->points[i].lat, src + 5, 4);   // offset 5
            memcpy(&out->points[i].depth, src + 9, 4); // offset 9
            
            src += 13; // 手动步进 13 字节（Waypoint packed 大小）
        }
        
        flag_task_ready = 0;
        return 1;
    }
    return 0;
}