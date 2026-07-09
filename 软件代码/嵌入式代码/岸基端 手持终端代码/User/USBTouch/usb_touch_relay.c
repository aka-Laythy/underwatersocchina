/**
 * USB 触摸屏解析和串口转发 - 统一版本
 * 适配 CH32V303CBT6 项目
 *
 * 硬件：
 * - USB: 触摸屏（I2C转USB芯片）
 * - USART3 (PB10/PB11): 发送触摸数据到光纤模块 → RDK X5
 * - UART1 (PA9): printf 调试
 * - Timer4: USB 轮询（10ms）
 *
 * 串口数据包格式（新版，6字节）：
 * [0x99] [xH] [xL] [yH] [yL] [checksum]
 * header = 0x99 (TOUCHSCREEN_PACKET_HEADER)
 * 校验和算法：只取低4位相加 (xH&0x0F) + (xL&0x0F) + (yH&0x0F) + (yL&0x0F)
 * 当 x,y ∈ [-50,50] 时，checksum ∈ [0,60]
 *
 * 调试开关：
 * - 定义 TOUCH_DEBUG_VERBOSE 可启用详细的HID数据和触摸事件输出
 */

#include "usb_touch_relay.h"
#include "usb_host_config.h"
#include "debug.h"
#include "string.h"

// 调试开关（可在这里或编译参数中定义）
#define TOUCH_DEBUG_VERBOSE

// 临时测试：禁用UART4实际发送，只打印调试信息（测试完后注释掉）
// #define DISABLE_UART4_SEND_FOR_TEST  // ✅ 已注释，启用真实串口发送

// 调试计数器
#ifdef TOUCH_DEBUG_VERBOSE
static uint32_t g_hid_packet_count = 0;
static uint32_t g_touch_event_count = 0;
#endif

// 调试信息显示数量限制（改大一点，方便调试）
#define DEBUG_PRINT_LIMIT 100  // 显示前100次触摸事件

/******************************************************************************
 * 触摸屏 HID 报告格式定义
 *
 * 根据HID Report Descriptor解析：
 * - Byte 0: Contact ID (Bit 0-5) + Tip Switch (Bit 6) + Padding (Bit 7)
 * - Byte 1-2: X坐标 (16 bits, little endian, 0-1024)
 * - Byte 3-4: Y坐标 (16 bits, little endian, 0-600)
 * 总计：5字节/触摸点
 ******************************************************************************/
#define TOUCH_REPORT_ID     0x15
#define TOUCH_POINT_SIZE    5    //  修正：5字节，不是6字节
#define MAX_TOUCH_POINTS    10

typedef struct {
    uint8_t  contact_status;  // Bit 0-5: Contact ID, Bit 6: Tip Switch, Bit 7: Padding
    uint16_t x;               // X坐标 (小端序, 0-1024)
    uint16_t y;               // Y坐标 (小端序, 0-600)
} __attribute__((packed)) TouchPoint_t;

/******************************************************************************
 * 串口协议定义 - TouchScreen_Packet
 ******************************************************************************/
#define TOUCHSCREEN_PACKET_HEADER  0x99

typedef struct {
    int8_t  header;       // 0x99 (固定包头)
    int8_t  xH;           // X 坐标高8位
    int8_t  xL;           // X 坐标低8位
    int8_t  yH;           // Y 坐标高8位
    int8_t  yL;           // Y 坐标低8位
    int8_t  checksum;     // 校验和: (xH&0x0F)+(xL&0x0F)+(yH&0x0F)+(yL&0x0F)
} __attribute__((packed)) TouchScreen_Packet;

/******************************************************************************
 * USART3 初始化（PB10=TX, PB11=RX）
 ******************************************************************************/
void UART3_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    // 1. 使能时钟（GPIOB + USART3 + AFIO复用功能）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    // 2. 配置 USART3 TX: PB10（复用推挽输出）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 配置 USART3 RX: PB11（浮空输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 4. 初始化 USART3
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART3, &USART_InitStructure);

    // 5. 使能 USART3
    USART_Cmd(USART3, ENABLE);

    // 6. 等待硬件初始化完成
    for (volatile int i = 0; i < 1000; i++);

    // 7. 清除标志位
    USART_ClearFlag(USART3, USART_FLAG_TC | USART_FLAG_RXNE);

#ifdef TOUCH_DEBUG_VERBOSE
    printf("USART3 init: SR=0x%04X (TXE=%d TC=%d)\r\n",
           USART3->STATR,
           (USART3->STATR & USART_FLAG_TXE) ? 1 : 0,
           (USART3->STATR & USART_FLAG_TC) ? 1 : 0);
#endif
}

/******************************************************************************
 * 串口发送触摸数据包 - 新格式
 * 数据包: [0x99] [xH] [xL] [yH] [yL] [checksum]
 * 校验和: 只取低4位相加 (xH&0x0F) + (xL&0x0F) + (yH&0x0F) + (yL&0x0F)
 ******************************************************************************/
void Send_Touch_Packet(uint8_t tip_switch, uint16_t x, uint16_t y)
{
    TouchScreen_Packet pkt;

    pkt.header = TOUCHSCREEN_PACKET_HEADER;

    // 拆分 16位 x 坐标为高低字节
    pkt.xH = (int8_t)((x >> 8) & 0xFF);  // 高8位
    pkt.xL = (int8_t)(x & 0xFF);         // 低8位

    // 拆分 16位 y 坐标为高低字节
    pkt.yH = (int8_t)((y >> 8) & 0xFF);  // 高8位
    pkt.yL = (int8_t)(y & 0xFF);         // 低8位

    // 校验和算法: 只取低4位相加
    pkt.checksum = (pkt.xH & 0x0F) + (pkt.xL & 0x0F) + (pkt.yH & 0x0F) + (pkt.yL & 0x0F);

#ifdef TOUCH_DEBUG_VERBOSE
    static uint32_t tx_count = 0;
    tx_count++;
    if (tx_count <= 10)
    {
        printf("[TX-START#%lu] X=%d(0x%02X%02X) Y=%d(0x%02X%02X) Checksum=%d USART3_SR=0x%04X\r\n",
               tx_count, x, (uint8_t)pkt.xH, (uint8_t)pkt.xL, y, (uint8_t)pkt.yH, (uint8_t)pkt.yL, pkt.checksum, USART3->STATR);
    }
#endif

    // 发送到 USART3（带超时保护，避免卡死）
    uint8_t *buf = (uint8_t *)&pkt;

#ifdef DISABLE_UART4_SEND_FOR_TEST
    // 临时测试：跳过实际发送，只打印数据包
#ifdef TOUCH_DEBUG_VERBOSE
    if (tx_count <= 10)
    {
        printf("[TX-SKIP#%lu] Would send: ", tx_count);
        for (int i = 0; i < sizeof(TouchScreen_Packet); i++)
        {
            printf("%02X ", buf[i]);
        }
        printf("\r\n");
    }
#endif
#else
    // 实际发送 USART3 数据（改进版，更稳定）
    uint32_t timeout;
    for (int i = 0; i < sizeof(TouchScreen_Packet); i++)
    {
        // 等待发送缓冲区空闲（带超时保护）
        timeout = 100000;  // 增大超时值
        while ((USART3->STATR & USART_FLAG_TXE) == 0)
        {
            if (--timeout == 0)
            {
#ifdef TOUCH_DEBUG_VERBOSE
                printf("[TX-TIMEOUT] Byte %d stuck, SR=0x%04X\r\n", i, USART3->STATR);
#endif
                return;  // 超时退出
            }
        }

        // 发送数据
        USART3->DATAR = buf[i];
    }

    // 等待最后一个字节发送完成
    timeout = 100000;
    while ((USART3->STATR & USART_FLAG_TC) == 0)
    {
        if (--timeout == 0) break;
    }

#ifdef TOUCH_DEBUG_VERBOSE
    if (tx_count <= 10)
    {
        printf("[TX-END#%lu] OK\r\n", tx_count);
    }
#endif
#endif
}

/******************************************************************************
 * 去重逻辑 - 静态变量保存上一次的触摸状态
 ******************************************************************************/
static uint16_t last_x = 0xFFFF;
static uint16_t last_y = 0xFFFF;
static uint8_t last_tip = 0xFF;

// 坐标容差
#define TOUCH_TOLERANCE 40

/******************************************************************************
 * 辅助函数：判断坐标是否在容差范围内（认为是同一点）
 ******************************************************************************/
static inline uint8_t is_same_position(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t dx = (int16_t)x1 - (int16_t)x2;
    int16_t dy = (int16_t)y1 - (int16_t)y2;

    // 使用绝对值比较（避免使用 abs 函数）
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    return (dx <= TOUCH_TOLERANCE && dy <= TOUCH_TOLERANCE);
}

/******************************************************************************
 * 解析触摸屏 HID 报告 - 带去重功能
 ******************************************************************************/
void Parse_Touch_Report(uint8_t *data, uint16_t len)
{
#ifdef TOUCH_DEBUG_VERBOSE
    g_hid_packet_count++;

    // ===== 调试：打印前16字节原始数据 =====
    if (g_hid_packet_count <= DEBUG_PRINT_LIMIT)
    {
        printf("[HID#%lu] Len=%d First16Bytes: ", g_hid_packet_count, len);
        for (int i = 0; i < (len < 16 ? len : 16); i++)
        {
            printf("%02X ", data[i]);
        }
        printf("\r\n");
    }
#endif

    // ===== 验证长度 =====
    if (len < 2)
    {
#ifdef TOUCH_DEBUG_VERBOSE
        if (g_hid_packet_count <= 5)
        {
            printf("[WARN] HID packet too short: %d bytes\r\n", len);
        }
#endif
        return;
    }

    // ===== 临时:跳过 Report ID 检查,直接解析 =====
    // 注释掉Report ID检查,看看能否解析到触摸数据
    // if (data[0] != TOUCH_REPORT_ID)
    // {
    //     return;
    // }

    // ===== 解析触摸点数 =====
    uint8_t contact_count = data[len - 1];

    if (contact_count == 0)
    {
        // 无触摸，只在状态变化时发送抬起事件
        if (last_tip != 0)
        {
            Send_Touch_Packet(0, last_x, last_y);  // 发送抬起时保持最后的坐标
            last_tip = 0;
            // 注意：不重置 last_x 和 last_y，保留最后触摸位置
#ifdef TOUCH_DEBUG_VERBOSE
            printf("[DEDUP] Touch release sent (last pos: %d,%d)\r\n", last_x, last_y);
#endif
        }
        return;
    }

    if (contact_count > MAX_TOUCH_POINTS)
    {
        printf("[WARN] Invalid contact count: %d\r\n", contact_count);
        return;
    }

    // ===== 解析第一个触摸点 =====
    uint8_t offset = 1;
    TouchPoint_t *point = (TouchPoint_t *)&data[offset];

    uint8_t tip_switch = (point->contact_status & 0x40) ? 1 : 0;
    uint16_t x = point->x;
    uint16_t y = point->y;

#ifdef TOUCH_DEBUG_VERBOSE
    g_touch_event_count++;
    if (g_touch_event_count <= 10)
    {
        printf("[PARSE-OK#%lu] Tip=%d X=%d Y=%d Count=%d\r\n",
               g_touch_event_count, tip_switch, x, y, contact_count);
    }
#endif

    // ===== 去重逻辑：智能判断是否需要发送 =====
    uint8_t should_send = 0;

    // 情况1：状态变化（抬起→按下 或 按下→抬起）
    if (tip_switch != last_tip)
    {
        should_send = 1;
#ifdef TOUCH_DEBUG_VERBOSE
        if (g_touch_event_count <= 10)
        {
            printf("[DEDUP] State changed (%d->%d), will send\r\n", last_tip, tip_switch);
        }
#endif
    }
    // 情况2：保持按下状态，但坐标移动超过容差范围
    else if (tip_switch == 1 && !is_same_position(x, y, last_x, last_y))
    {
        should_send = 1;
#ifdef TOUCH_DEBUG_VERBOSE
        if (g_touch_event_count <= 10)
        {
            printf("[DEDUP] Position moved (%d,%d)->(%d,%d), will send\r\n", last_x, last_y, x, y);
        }
#endif
    }
    // 情况3：重复数据，跳过
    else
    {
#ifdef TOUCH_DEBUG_VERBOSE
        if (g_touch_event_count <= 10)
        {
            printf("[DEDUP] Duplicate (Tip=%d X=%d Y=%d), skipped\r\n", tip_switch, x, y);
        }
#endif
    }

    // 发送数据并更新状态
    if (should_send)
    {
        Send_Touch_Packet(tip_switch, x, y);
        last_tip = tip_switch;
        last_x = x;
        last_y = y;
    }
}

/******************************************************************************
 * 调试：打印 USB 枚举状态
 ******************************************************************************/
void Print_USB_Status(void)
{
#ifdef TOUCH_DEBUG_VERBOSE
    extern struct _ROOT_HUB_DEVICE RootHubDev[];
    extern struct __HOST_CTL HostCtl[];

    printf("\n=== USB Status ===\n");

    for (int port = 0; port < DEF_TOTAL_ROOT_HUB; port++)
    {
        printf("Port%d: Status=0x%02X, Type=0x%02X\n",
               port, RootHubDev[port].bStatus, RootHubDev[port].bType);

        if (RootHubDev[port].bStatus >= ROOT_DEV_SUCCESS)
        {
            uint8_t idx = RootHubDev[port].DeviceIndex;
            printf("  Device Index=%d, IntfNum=%d\n",
                   idx, HostCtl[idx].InterfaceNum);

            for (int intf = 0; intf < HostCtl[idx].InterfaceNum; intf++)
            {
                printf("    Intf%d: InEPs=%d, Type=0x%02X\n",
                       intf,
                       HostCtl[idx].Interface[intf].InEndpNum,
                       HostCtl[idx].Interface[intf].Type);
            }
        }
    }
    printf("==================\n\n");
#endif
}
