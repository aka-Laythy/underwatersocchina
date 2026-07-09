#include "debug.h"
#include "string.h"

#include "PwrBoard.h"


/* 全局变量 - 电流值（单位mA） */
volatile PWR_Monitor_t g_pwr = {0, };

/* 接收状态机与缓冲 */
#define PWR_FRAME_LEN   45        // 帧总长度 PWR>>>...mA.\r\n
static u8 pwr_rx_buf[PWR_FRAME_LEN];
static volatile u8 pwr_rx_idx = 0;
static volatile u8 pwr_sync_state = 0;  // 0:找P, 1:找W, 2:找R, 3:接收数据

/* 初始化函数 */
void Pwr_Conn_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    /* 1. 使能时钟: UART5在APB1, GPIOC/GPIOD在APB2 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE);
    /* 2. 配置PC12(TX) - 复用推挽 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    /* 3. 配置PD2(RX) - 浮空输入（或改为GPIO_Mode_IPU上拉输入） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    /* 4. UART5参数配置 - 默认115200，可根据实际修改 */
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(UART5, &USART_InitStructure);
    /* 5. 使能接收中断（RXNE） */
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);
    /* 6. NVIC配置 - 抢占优先级1，响应优先级0 */
    NVIC_InitStructure.NVIC_IRQChannel = UART5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    /* 7. 使能UART5 */
    USART_Cmd(UART5, ENABLE);
}

/* UART5中断服务程序 - 使用WCH中断属性 */
void UART5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART5_IRQHandler(void)
{
    if(USART_GetITStatus(UART5, USART_IT_RXNE) != RESET)
    {
        u8 ch = USART_ReceiveData(UART5);
        switch(pwr_sync_state)
        {
            case 0: // 等待帧头 'P'
                if(ch == 'P') {
                    pwr_rx_buf[0] = 'P';
                    pwr_sync_state = 1;
                }
                break;
                
            case 1: // 等待 'W'
                if(ch == 'W') {
                    pwr_rx_buf[1] = 'W';
                    pwr_sync_state = 2;
                } else if(ch != 'P') {  // 如果不是P则退回初始状态（允许PP连续检测）
                    pwr_sync_state = 0;
                }
                break;
                
            case 2: // 等待 'R'
                if(ch == 'R') {
                    pwr_rx_buf[2] = 'R';
                    pwr_sync_state = 3;
                    pwr_rx_idx = 3;     // 准备接收后续数据
                } else if(ch == 'P') {
                    pwr_sync_state = 1; // PWRP -> 可能是PPW，回到状态1
                } else {
                    pwr_sync_state = 0;
                }
                break;
                
            case 3: // 接收剩余42字节（45-3=42）
                pwr_rx_buf[pwr_rx_idx++] = ch;
                if(pwr_rx_idx >= PWR_FRAME_LEN) {
                    /* 帧接收完成，验证并解析 */
                    if(pwr_rx_buf[0]=='P' && pwr_rx_buf[1]=='W' && pwr_rx_buf[2]=='R') {
                        /* 固定位置解析（零开销，无需sscanf） */
                        // VBUS: offset 12, len 5  (PWR>>> VBUS:12345mA...)
                        g_pwr.vbus_mA = (pwr_rx_buf[12]-'0')*10000 + 
                                       (pwr_rx_buf[13]-'0')*1000 + 
                                       (pwr_rx_buf[14]-'0')*100 + 
                                       (pwr_rx_buf[15]-'0')*10 + 
                                       (pwr_rx_buf[16]-'0');
                        
                        // 5V: offset 24, len 4 (...mA, 5V:1234mA...)
                        g_pwr.bus_5v_mA = (pwr_rx_buf[24]-'0')*1000 + 
                                     (pwr_rx_buf[25]-'0')*100 + 
                                     (pwr_rx_buf[26]-'0')*10 + 
                                     (pwr_rx_buf[27]-'0');
                        
                        // 12V: offset 36, len 4 (...mA, 12V:1234mA.\r\n)
                        g_pwr.bus_12v_mA = (pwr_rx_buf[36]-'0')*1000 + 
                                      (pwr_rx_buf[37]-'0')*100 + 
                                      (pwr_rx_buf[38]-'0')*10 + 
                                      (pwr_rx_buf[39]-'0');
                    }
                    /* 无论校验是否通过，都重置状态机准备接收下一帧 */
                    pwr_sync_state = 0;
                    pwr_rx_idx = 0;
                    g_pwr.updated    = 1;
                }
                break;
            default:
                pwr_sync_state = 0;
                pwr_rx_idx = 0;
                break;
        }
        USART_ClearITPendingBit(UART5, USART_IT_RXNE);
    }
}
