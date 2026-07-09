#include "debug.h"
#include "Fiber.h"

// 发送状态机
static volatile uint8_t tx_buf[6];
static volatile uint8_t tx_cnt = 0;
static volatile uint8_t tx_busy = 0;

// 接收缓冲区（如需解析返回数据）
static volatile uint8_t rx_buf[6];
static volatile uint8_t rx_cnt = 0;

volatile RemoteControl_Packet rc_packet = {0, };

/**
 * @brief 计算校验和：只取低4位相加
 * @note  与接收端算法保持一致
 */
static uint8_t CalcChecksum_Low4Bit(int8_t x, int8_t y, int8_t z, int8_t yaw)
{
    // 强制转为uint8_t后再取低4位，正确处理负数（如-1 -> 0xFF -> 0x0F）
    return ((uint8_t)x & 0x0F) + ((uint8_t)y & 0x0F) 
         + ((uint8_t)z & 0x0F) + ((uint8_t)yaw & 0x0F);
}

void Fiber_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;
    // 1. 时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);   // GPIOB在APB2
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);  // USART3在APB1
    // 2. GPIO配置
    // PB10 TX - 复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    // PB11 RX - 浮空输入（或带上拉 GPIO_Mode_IPU，视硬件接线而定）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    // 3. USART配置
    USART_InitStructure.USART_BaudRate = 357000;                    // 可根据需要修改波特率
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 全双工
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART3, &USART_InitStructure);
    // 4. 中断配置（CH32V30x 使用 PFIC，但库函数封装为 NVIC_Init）
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;  // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;         // 子优先级
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    // 5. 使能接收中断（RXNE）
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    // 使能USART
    USART_Cmd(USART3, ENABLE);
}

/**
 * @brief 启动发送一帧数据（非阻塞）
 */
u8 Fiber_Send_RemoteControl(int8_t x, int8_t y, int8_t z, int8_t yaw)
{
    // 如果正在发送，返回忙标志
    //if (tx_busy) return 1;
    tx_busy = 1;
    tx_cnt = 0;
    // 填充帧缓冲区
    tx_buf[0] = REMOTECONTROL_PACKET_HEADER;  // 0x66
    tx_buf[1] = (uint8_t)x;
    tx_buf[2] = (uint8_t)y;
    tx_buf[3] = (uint8_t)z;
    tx_buf[4] = (uint8_t)yaw;
    tx_buf[5] = CalcChecksum_Low4Bit(x, y, z, yaw);  // 低4位校验和
    // 启动发送：写入第一个字节，使能TXE中断
    USART_SendData(USART3, tx_buf[0]);
    USART_ITConfig(USART3, USART_IT_TXE, ENABLE);
    return 0;
}

/**
 * @brief USART3 中断服务函数
 * @note  处理发送(TXE)和接收(RXNE)中断
 */
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void)
{
    // ---------- 接收处理 ----------
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USART3);
        
        // 简单帧接收状态机（示例：假设连续收到6字节为一帧）
        rx_buf[rx_cnt++] = data;
        if (rx_cnt >= 6) {
            rx_cnt = 0;
            // 此处可设置标志位通知主循环处理，或调用解析函数
            // 例如：Frame_Received_Flag = 1;
        }
        // 清除中断标志（读DR自动清除，但保险起见）
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
    // ---------- 发送处理 ----------
    if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET)
    {
        tx_cnt++;
        if (tx_cnt < 6) {
            // 继续发送下一字节
            USART_SendData(USART3, tx_buf[tx_cnt]);
        } else {
            // 全部发送完毕，关闭TXE中断，释放忙标志
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);
            tx_busy = 0;
        }
        USART_ClearITPendingBit(USART3, USART_IT_TXE);
    }
    // 错误处理（可选）：清除过载错误标志
    if (USART_GetITStatus(USART3, USART_IT_ORE) != RESET)
    {
        (void)USART_ReceiveData(USART3);  // 读DR清除ORE
        USART_ClearITPendingBit(USART3, USART_IT_ORE);
    }
}
