#include "ch32v30x.h"
#include "Control.h"

#include "Fiber.h"

// 光纤通信用的UART4

// 全局滴答（由TIM6中断更新）
volatile uint32_t fiber_ticks_ms = 0;
// 超时使能标志
volatile uint8_t timeout_enabled = 1; // 默认启用
// 接收状态
volatile uint8_t rx_buffer[6];
volatile uint8_t rx_index = 0;
volatile uint8_t error_count = 0;
volatile uint8_t valid_frame_received = 0; // 标记有效帧
// for debug
int8_t g_fiber_latest_x;
int8_t g_fiber_latest_y;
int8_t g_fiber_latest_z;
int8_t g_fiber_latest_yaw;
uint32_t g_fiber_latest_last_rx_tick;  // 记录最后一次接收时间戳

void Fiber_EnableTimeout(void) {timeout_enabled = 1;}

void Fiber_DisableTimeout(void) {timeout_enabled = 0;}

int Fiber_IsTimeoutEnabled(void) {return timeout_enabled;}

// 内联函数：校验和（处理volatile）
// 新算法：只取低4位相加，when input x,y,z,yaw in [-50,50]，output checksum in [0,60]
static inline uint8_t calc_checksum(const volatile uint8_t* buf)
{
    return (buf[1] & 0x0F) + (buf[2] & 0x0F) + (buf[3] & 0x0F) + (buf[4] & 0x0F);
    // buf[1]=x, buf[2]=y, buf[3]=z, buf[4]=yaw，buf[5]=checksum
}

void Fiber_Init(void)
{
    // 初始化TIM6滴答
    // Fiber_TimerInit();  //无需在这里初始化，解耦了，只需要保证TIM6中断先于Fiber_Init执行即可！

    // 初始化UART4
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;;
    GPIO_Init(GPIOC, &gpio);

    USART_InitTypeDef usart;
    usart.USART_BaudRate = BAUD_RATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(UART4, &usart);

    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = UART4_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(UART4, USART_IT_RXNE, ENABLE);
    USART_Cmd(UART4, ENABLE);
    printf("[Fiber]>>> Init SUCCESS.\r\n");
}

// UART4 中断
void UART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART4_IRQHandler(void)
{
    if(USART_GetITStatus(UART4, USART_IT_RXNE) != RESET)
    {
        uint8_t byte = USART_ReceiveData(UART4);

        if(rx_index == 0)
        {
            if(byte == REMOTECONTROL_PACKET_HEADER)
            {
                rx_buffer[0] = byte;
                rx_index = 1;
            }
        }
        else
        {
            rx_buffer[rx_index] = byte;
            rx_index++;

            if(rx_index >= 6)
            {
                uint8_t checksum = calc_checksum(rx_buffer);
                if(checksum == rx_buffer[5])
                {
                    int8_t x   = rx_buffer[1];     // 原来是 *(int16_t*)&rx_buffer[1]，现在直接取，下同。
                    int8_t y   = rx_buffer[2];     // 原来是 *(int16_t*)&rx_buffer[3]
                    int8_t z   = rx_buffer[3];     // 原来是 rx_buffer[5]
                    int8_t yaw = rx_buffer[4];     // 原来是 rx_buffer[6]
                    // 更新全局调试变量（新增）
                    g_fiber_latest_x = x;
                    g_fiber_latest_y = y;
                    g_fiber_latest_z = z;
                    g_fiber_latest_yaw = yaw;
                    g_fiber_latest_last_rx_tick = fiber_ticks_ms;
                    Control_Update(x, y, z, yaw);  // 收到一帧合法数据，立刻提交给control.c执行
                    valid_frame_received = 1;
                    error_count = 0;
                    // printf("[Fiber]>>> Recv a valid frame.");
                }
                else
                {
                    error_count++;
                    if(error_count >= MAX_ERROR_COUNT)
                    {
                        Control_Neutral();
                        // printf("[Fiber]>>> control signal timeout, set control neutral.");
                    }
                }
                rx_index = 0;
            }
        }
    }
}

// 按照N ms循环调用，则最差在TIMEOUT_MS之后 + N ms进入悬停。
void Fiber_Process(void)
{
    static uint32_t last_valid_time = 0;

    if(valid_frame_received)
    {
        last_valid_time = fiber_ticks_ms;
        valid_frame_received = 0;
    }

    // 仅当超时启用时才执行悬停
    if(timeout_enabled && (fiber_ticks_ms - last_valid_time > TIMEOUT_MS))
    {
        Control_Neutral();  // 如果要把 Fiber_Process 函数写到定时器中断里面执行，务必确定 Control_Neutral 函数绝对精简、绝对可控（耗时够短）
        error_count = 0;
        last_valid_time = fiber_ticks_ms; // 防止重复触发
    }
}

/* 添加调试打印函数（放在 Fiber_Process 函数之后或任意位置）： */
void Fiber_Debug_Print(void)
{
    uint32_t elapsed = fiber_ticks_ms - g_fiber_latest_last_rx_tick;
    printf("[Fiber Debug] X:%02d, Y:%02d, Z:%02d, Yaw:%02d, %ums ago\r\n",
           g_fiber_latest_x, 
           g_fiber_latest_y, 
           g_fiber_latest_z, 
           g_fiber_latest_yaw,
           elapsed);
}