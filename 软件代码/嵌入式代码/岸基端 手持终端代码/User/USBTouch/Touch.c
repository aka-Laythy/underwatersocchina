/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : WCH / Modified for Touch Relay
* Version            : V1.2.0
* Date               : 2024/xx/xx
* Description        : USB 触摸屏中继主程序 - CH32V303CBT6
*                      UART1 (PA9):    printf 调试
*                      USART3 (PB10):  触摸数据发送 → 光纤 → RDK X5 (0x99 header)
*                      Timer4:         USB 轮询（10ms）
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
*******************************************************************************/

#include "debug.h"
#include "usb_touch_relay.h"
#include "usb_host_config.h"

/******************************************************************************
 * Timer4 中断 - 用于 USB HID 轮询（10ms）
 ******************************************************************************/
void TIM4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM4_IRQHandler(void)
{
    uint8_t usb_port;
    uint8_t index;
    uint8_t intf_num, in_num;

    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

        // 轮询所有 USB 端口的 HID 设备
        for (usb_port = 0; usb_port < DEF_TOTAL_ROOT_HUB; usb_port++)
        {
            if (RootHubDev[usb_port].bStatus >= ROOT_DEV_SUCCESS)
            {
                index = RootHubDev[usb_port].DeviceIndex;

                if (RootHubDev[usb_port].bType == USB_DEV_CLASS_HID)
                {
                    for (intf_num = 0; intf_num < HostCtl[index].InterfaceNum; intf_num++)
                    {
                        for (in_num = 0; in_num < HostCtl[index].Interface[intf_num].InEndpNum; in_num++)
                        {
                            HostCtl[index].Interface[intf_num].InEndpTimeCount[in_num]++;
                        }
                    }
                }
            }
        }
    }
}

/******************************************************************************
 * Timer4 初始化（10ms 中断，100Hz 轮询）
 ******************************************************************************/
void TIM4_Init(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    TIM_TimeBaseStructure.TIM_Period = arr;
    TIM_TimeBaseStructure.TIM_Prescaler = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM4, ENABLE);
    NVIC_EnableIRQ(TIM4_IRQn);
}

u8 Touch_Init(void)
{
    // 初始化 USB Host
    #if DEF_USBFS_PORT_EN
		printf("USBFS Host initializing...\r\n");
		USBFS_RCC_Init();
		USBFS_Host_Init(ENABLE);
		memset(RootHubDev, 0, sizeof(ROOT_HUB_DEVICE) * DEF_TOTAL_ROOT_HUB);
		memset(HostCtl, 0, sizeof(HOST_CTL) * DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL);
		printf("USBFS Host ready\r\n");
    #endif
    TIM4_Init(10000 - 1, 144 - 1);  // 144MHz / 144 = 1MHz, 10000 = 10ms
    return 1;
}

void Touch_Deal(void)
{
    // 注意：USBH_MainDeal() 内部已经调用了 Parse_Touch_Report()
    USBH_MainDeal();
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主程序 - USB 触摸屏中继
 *
 * @return  none
 */

 /*
int main1(void)
{
    // 初始化 USB Host
    #if DEF_USBFS_PORT_EN
    printf("USBFS Host initializing...\r\n");
    USBFS_RCC_Init();
    USBFS_Host_Init(ENABLE);
    memset(RootHubDev, 0, sizeof(ROOT_HUB_DEVICE) * DEF_TOTAL_ROOT_HUB);
    memset(HostCtl, 0, sizeof(HOST_CTL) * DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL);
    printf("USBFS Host ready\r\n");
    #endif

    // 初始化 Timer4（10ms 中断用于 USB 轮询）
    TIM4_Init(10000 - 1, 144 - 1);  // 144MHz / 144 = 1MHz, 10000 = 10ms
    printf("Timer4 initialized (10ms polling)\r\n");

    printf("\nWaiting for USB touch device...\r\n");

    // uint32_t status_print_tick = 0;  // 已注释：不再打印周期性状态

    while(1)
    {
        // USB 主机任务 - 枚举设备和处理 HID 数据
        // 注意：USBH_MainDeal() 内部已经调用了 Parse_Touch_Report()
        //USBH_MainDeal();

        // ===== 已注释：每 5 秒打印一次 USB 状态（太烦人）=====
        // status_print_tick++;
        // if (status_print_tick >= 5000)
        // {
        //     status_print_tick = 0;
        //     printf("\n[Status] USB Port0: bStatus=0x%02X, bType=0x%02X\r\n",
        //            RootHubDev[0].bStatus, RootHubDev[0].bType);
        //
        //     if (RootHubDev[0].bStatus >= ROOT_DEV_SUCCESS)
        //     {
        //         uint8_t idx = RootHubDev[0].DeviceIndex;
        //         printf("         Device OK, IntfNum=%d\r\n", HostCtl[idx].InterfaceNum);
        //
        //         if (RootHubDev[0].bType == USB_DEV_CLASS_HID)
        //         {
        //             printf("         HID Device detected!\r\n");
        //             for (int i = 0; i < HostCtl[idx].InterfaceNum; i++)
        //             {
        //                 printf("           Intf%d: InEPs=%d\r\n", i,
        //                        HostCtl[idx].Interface[i].InEndpNum);
        //             }
        //         }
        //     }
        // }

        //Delay_Ms(1);
    }
}
*/
