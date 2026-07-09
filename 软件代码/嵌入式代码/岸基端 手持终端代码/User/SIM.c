#include "sim.h"
#include "debug.h"  // 用于调试（可选）

// 接收环形缓冲区
volatile uint8_t sim_rx_buf[SIM_RX_BUF_SIZE] = {0};
volatile uint16_t sim_rx_head = 0;
volatile uint16_t sim_rx_tail = 0;

// 发送环形缓冲区
static volatile uint8_t sim_tx_buf[SIM_TX_BUF_SIZE] = {0};
static volatile uint16_t sim_tx_head = 0;
static volatile uint16_t sim_tx_tail = 0;

/**
 * @brief 初始化USART2 (PA2=TX, PA3=RX) @ 115200 8N1
 */
void SIM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    // 1. 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    // 2. GPIO配置
    // PA2 - USART2_TX (复用推挽输出)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA3 - USART2_RX (浮空输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART2配置
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    // 4. 使能中断
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);  // 接收中断
    USART_ITConfig(USART2, USART_IT_TXE, DISABLE);  // 发送中断初始禁用
    USART_Cmd(USART2, ENABLE);

    // 5. NVIC配置
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief 非阻塞发送AT指令（自动添加\r\n）
 * @param cmd 指令字符串，如 "AT"、"AT+CSQ"
 */
void SIM_SendCmd(const char *cmd)
{
    if(!cmd) return;

    // 1. 将 cmd + "\r\n" 存入发送缓冲区
    uint16_t len = 0;
    while(cmd[len] && len < (SIM_TX_BUF_SIZE - 3)) len++; // 保留3字节给\r\n\0

    // 关中断保护（防止中断中修改head/tail）
    __disable_irq();
    
    // 检查缓冲区空间
    uint16_t space = (sim_tx_tail > sim_tx_head) ? 
                     (sim_tx_tail - sim_tx_head - 1) : 
                     (SIM_TX_BUF_SIZE - (sim_tx_head - sim_tx_tail) - 1);
    
    if(space >= (len + 2)) { // +2 for \r\n
        // 复制指令
        for(uint16_t i = 0; i < len; i++) {
            sim_tx_buf[sim_tx_head] = cmd[i];
            sim_tx_head = (sim_tx_head + 1) % SIM_TX_BUF_SIZE;
        }
        // 添加\r\n
        sim_tx_buf[sim_tx_head] = '\r';
        sim_tx_head = (sim_tx_head + 1) % SIM_TX_BUF_SIZE;
        sim_tx_buf[sim_tx_head] = '\n';
        sim_tx_head = (sim_tx_head + 1) % SIM_TX_BUF_SIZE;
        
        // 触发首次发送
        if(!USART_GetFlagStatus(USART2, USART_FLAG_TXE)) {
            USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
        }
        // 【关键修复】无条件使能 TXE 中断（确保数据被发送）
        USART_ITConfig(USART2, USART_IT_TXE, ENABLE);  // ← 修复点
    }
    
    __enable_irq();
}

/**
 * @brief USART2中断服务函数
 */
void USART2_IRQHandler(void) __attribute__((interrupt));
void USART2_IRQHandler(void)
{
    uint8_t data;

    // 接收中断
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        data = USART_ReceiveData(USART2);
        
        // 存入接收缓冲区（覆盖式，避免死锁）
        uint16_t next_head = (sim_rx_head + 1) % SIM_RX_BUF_SIZE;
        if(next_head != sim_rx_tail) { // 有空间
            sim_rx_buf[sim_rx_head] = data;
            sim_rx_head = next_head;
        } else {
            // 缓冲区满，覆盖最旧数据（保证最新数据不丢失）
            sim_rx_buf[sim_rx_head] = data;
            sim_rx_head = next_head;
            sim_rx_tail = (sim_rx_tail + 1) % SIM_RX_BUF_SIZE; // 丢弃最旧
        }
        
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }

    // 发送中断
    if(USART_GetITStatus(USART2, USART_IT_TXE) != RESET) {
        if(sim_tx_head != sim_tx_tail) {
            // 有数据待发送
            USART_SendData(USART2, sim_tx_buf[sim_tx_tail]);
            sim_tx_tail = (sim_tx_tail + 1) % SIM_TX_BUF_SIZE;
        } else {
            // 发送完成，禁用TXE中断
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
        }
        USART_ClearITPendingBit(USART2, USART_IT_TXE);
    }
}

/**
 * @brief 从接收缓冲区提取完整一行（以\r\n结尾，自动过滤0x00和空行）
 * @param buf 目标缓冲区
 * @param max_len 缓冲区大小（建议≥128）
 * @return 实际提取的字符数（不含\r\n），0=无完整行
 */
uint16_t SIM_GetLine(uint8_t *buf, uint16_t max_len)
{
    if (!buf || max_len == 0) return 0;

    __disable_irq();
    uint16_t avail = (sim_rx_head >= sim_rx_tail) ? 
                     (sim_rx_head - sim_rx_tail) : 
                     (SIM_RX_BUF_SIZE - sim_rx_tail + sim_rx_head);
    
    // 至少需要2字节（\r\n）
    if (avail < 2) {
        __enable_irq();
        return 0;
    }

    // 在环形缓冲区中查找 \r\n（支持跨边界）
    uint16_t pos = sim_rx_tail;
    uint16_t found_pos = SIM_RX_BUF_SIZE; // 无效值
    uint16_t i = 0;
    
    while (i < avail) {
        if (sim_rx_buf[pos] == '\r') {
            uint16_t next_pos = (pos + 1) % SIM_RX_BUF_SIZE;
            if (sim_rx_buf[next_pos] == '\n') {
                found_pos = pos; // \r 的位置
                break;
            }
        }
        pos = (pos + 1) % SIM_RX_BUF_SIZE;
        i++;
    }

    if (found_pos == SIM_RX_BUF_SIZE) {
        __enable_irq();
        return 0; // 未找到 \r\n
    }

    // 提取 \r 之前的数据（过滤 0x00 和空格行）
    uint16_t start = sim_rx_tail;
    uint16_t len = 0;
    uint8_t has_non_space = 0;
    
    while (start != found_pos) {
        uint8_t c = sim_rx_buf[start];
        if (c != 0x00) { // 过滤 0x00
            if (c != ' ' && c != '\t') has_non_space = 1;
            if (len < max_len - 1) {
                buf[len++] = c;
            }
        }
        start = (start + 1) % SIM_RX_BUF_SIZE;
    }

    // 跳过 \r\n
    sim_rx_tail = (found_pos + 2) % SIM_RX_BUF_SIZE;

    __enable_irq();

    // 过滤空行（仅空格/制表符）
    if (!has_non_space) return 0;

    buf[len] = '\0';
    return len;
}
