#include "debug.h"
#include <stdlib.h>

#include "JY61P.h"
// JY61P接UART8 -- TX：PE14, RX：PE15 (第二复用)

// 全局 IMU 数据（自动在中断中更新）
volatile JY61P_Data_t g_imu_data = {0};

// UART8 接收缓冲区
static uint8_t uart8_rx_buffer[11];
static volatile uint8_t uart8_rx_index = 0;

// UART8 中断处理（自动解析所有包）
void UART8_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART8_IRQHandler(void)
{
    if (USART_GetITStatus(UART8, USART_IT_RXNE) != RESET)
    {
        uint8_t byte = USART_ReceiveData(UART8);
        
        // 状态机：寻找包头 0x55
        if (uart8_rx_index == 0)
        {
            if (byte == 0x55)
            {
                uart8_rx_buffer[0] = byte;
                uart8_rx_index = 1;
            }
        }
        else if (uart8_rx_index == 1)
        {
            // 第二个字节是包类型
            if (byte == 0x51 || byte == 0x52 || byte == 0x53 || byte == 0x54)
            {
                uart8_rx_buffer[1] = byte;
                uart8_rx_index = 2;
            }
            else
            {
                // 不是有效包类型，重新开始
                uart8_rx_index = 0;
            }
        }
        else
        {
            uart8_rx_buffer[uart8_rx_index] = byte;
            uart8_rx_index++;
            
            // 完整包接收完成（11字节）
            if (uart8_rx_index >= 11)
            {
                // 校验和验证：前10字节之和等于第11字节
                uint8_t sum = 0;
                for (int i = 0; i < 10; i++) 
                {
                    sum += uart8_rx_buffer[i];
                }
                
                if ((sum & 0xFF) == uart8_rx_buffer[10])
                {
                    uint8_t type = uart8_rx_buffer[1];
                    
                    if (type == 0x53)  // 角度包
                    {
                        // 小端序：低字节在前，高字节在后
                        int16_t roll_raw = (uart8_rx_buffer[3] << 8) | uart8_rx_buffer[2];
                        int16_t pitch_raw = (uart8_rx_buffer[5] << 8) | uart8_rx_buffer[4];
                        int16_t yaw_raw = (uart8_rx_buffer[7] << 8) | uart8_rx_buffer[6];
                        
                        // 转换为实际值：±180° 对应 ±32768
                        g_imu_data.roll_deg = roll_raw * 180.0f / 32768.0f;
                        g_imu_data.pitch_deg = pitch_raw * 180.0f / 32768.0f;
                        g_imu_data.yaw_deg = yaw_raw * 180.0f / 32768.0f;
                    }
                    else if (type == 0x51)  // 加速度包
                    {
                        int16_t ax_raw = (uart8_rx_buffer[3] << 8) | uart8_rx_buffer[2];
                        int16_t ay_raw = (uart8_rx_buffer[5] << 8) | uart8_rx_buffer[4];
                        int16_t az_raw = (uart8_rx_buffer[7] << 8) | uart8_rx_buffer[6];
                        
                        // 转换为实际值：±16g 对应 ±32768
                        g_imu_data.ax_g = ax_raw * 16.0f / 32768.0f;
                        g_imu_data.ay_g = ay_raw * 16.0f / 32768.0f;
                        g_imu_data.az_g = az_raw * 16.0f / 32768.0f;
                    }
                    else if (type == 0x52)  // 角速度包
                    {
                        int16_t gx_raw = (uart8_rx_buffer[3] << 8) | uart8_rx_buffer[2];
                        int16_t gy_raw = (uart8_rx_buffer[5] << 8) | uart8_rx_buffer[4];
                        int16_t gz_raw = (uart8_rx_buffer[7] << 8) | uart8_rx_buffer[6];
                        
                        // 转换为实际值：±2000°/s 对应 ±32768
                        g_imu_data.gx_dps = gx_raw * 2000.0f / 32768.0f;
                        g_imu_data.gy_dps = gy_raw * 2000.0f / 32768.0f;
                        g_imu_data.gz_dps = gz_raw * 2000.0f / 32768.0f;
                    }
                    // 0x54 是磁场包，这里忽略
                }
                // 重置状态机
                uart8_rx_index = 0;
            }
        }
    }
}

// 初始化 UART8（115200 bps）
static void JY61P_UART8_Init(void)
{
    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART8, ENABLE);
    
    // ========== 添加重映射配置 ==========
    // UART8 复用重映射到 PE14/PE15
    GPIO_PinRemapConfig(GPIO_FullRemap_USART8, ENABLE);
    // 260214 remark1: 必须用 GPIO_FullRemap_USART8，不能用 GPIO_PartialRemap_USART8！
    // initial note：可能的坑，数据手册明确的写了UART8的第2和第3重映射引脚都是PE14/PE15，我不明白这样是干什么，
    //       看了GPIO_PinRemapConfig函数的注释，只有用GPIO_PartialRemap_USART8和GPIO_FullRemap_USART8是UART8的，
    //       这是个可能的坑，在这里先注释下。
    // initial ask ai: https://www.kimi.com/chat/19c50a58-f152-84dc-8000-09c2dc381148
    //         https://chat.qwen.ai/c/5ac21cf5-773b-44ae-bf6e-1227b9cbb494

    // 配置 GPIO
    GPIO_InitTypeDef gpio;
    
    // PC2 - UART8_TX (虽然只用RX，但配置完整)
    gpio.GPIO_Pin = GPIO_Pin_14;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOE, &gpio);
    
    // PC3 - UART8_RX
    gpio.GPIO_Pin = GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOE, &gpio);
    
    // 配置 UART8
    USART_InitTypeDef usart;
    usart.USART_BaudRate = 115200;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 虽然只用Rx，但建议都开
    USART_Init(UART8, &usart);
    
    // 配置 NVIC
    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = UART8_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    
    // 使能接收中断
    USART_ITConfig(UART8, USART_IT_RXNE, ENABLE);
    USART_Cmd(UART8, ENABLE);
}

int JY61P_Init(void)
{
    JY61P_UART8_Init();
    // Delay_Ms(100);
    printf("[JY61P]>>> Init SUCCESS!\r\n");
    return 0;
}

void JY61P_Print(void)
{
    // 将浮点数据转换为整数（乘以100或1000，四舍五入）
    int yaw_int = (int)(g_imu_data.yaw_deg * 100 + (g_imu_data.yaw_deg >= 0 ? 0.5 : -0.5));
    int pitch_int = (int)(g_imu_data.pitch_deg * 100 + (g_imu_data.pitch_deg >= 0 ? 0.5 : -0.5));
    int roll_int = (int)(g_imu_data.roll_deg * 100 + (g_imu_data.roll_deg >= 0 ? 0.5 : -0.5));
    
    int ax_int = (int)(g_imu_data.ax_g * 1000 + (g_imu_data.ax_g >= 0 ? 0.5 : -0.5));
    int ay_int = (int)(g_imu_data.ay_g * 1000 + (g_imu_data.ay_g >= 0 ? 0.5 : -0.5));
    int az_int = (int)(g_imu_data.az_g * 1000 + (g_imu_data.az_g >= 0 ? 0.5 : -0.5));
    
    int gx_int = (int)(g_imu_data.gx_dps * 100 + (g_imu_data.gx_dps >= 0 ? 0.5 : -0.5));
    int gy_int = (int)(g_imu_data.gy_dps * 100 + (g_imu_data.gy_dps >= 0 ? 0.5 : -0.5));
    int gz_int = (int)(g_imu_data.gz_dps * 100 + (g_imu_data.gz_dps >= 0 ? 0.5 : -0.5));

    // 打印角度（度），保留2位小数
    printf("[JY61P]>>> Yaw: %d.%02d deg, Pitch: %d.%02d deg, Roll: %d.%02d deg\r\n",
           yaw_int / 100, abs(yaw_int % 100),
           pitch_int / 100, abs(pitch_int % 100),
           roll_int / 100, abs(roll_int % 100));

    // 打印加速度（g），保留3位小数
    printf("[JY61P]>>> Accx: %d.%03d g, Accy: %d.%03d g, Accz: %d.%03d g\r\n",
           ax_int / 1000, abs(ax_int % 1000),
           ay_int / 1000, abs(ay_int % 1000),
           az_int / 1000, abs(az_int % 1000));

    // 打印角速度（dps），保留2位小数
    printf("[JY61P]>>> AngVx: %d.%02d deg/s, AngVy: %d.%02d deg/s, AngVz: %d.%02d deg/s\r\n",
           gx_int / 100, abs(gx_int % 100),
           gy_int / 100, abs(gy_int % 100),
           gz_int / 100, abs(gz_int % 100));
}
