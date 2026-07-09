#include "string.h"
#include "debug.h"
#include "OLED.h"
#include "stick.h"
#include "SIM.h"
#include "Clock.h"
#include "Fiber.h"

#include "USBTouch/Touch.h"
#include "system_ch32v30x.h"

extern u32 tick_ms;
extern Stick_Data_t Stick1;
extern Stick_Data_t Stick2;
extern RemoteControl_Packet rc_packet;
u16 line_len;
u8 line_buf[256];
u8 sim_lock_1 = 0;
u32 touch_deal_time = 0;

int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();
	Delay_Init();

	USART_Printf_Init(115200);	
	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

	Clock_Init();
	SIM_Init();
	OLED_Init();
	Fiber_Init();
	Stick_Init();
	Stick_Start();
	Touch_Init();

	u8 stick_adc_debug = 1;
	if(stick_adc_debug == 1)
	{
		OLED_ShowString(0, 0, "S1  LR   UD  BTN", OLED_6X8);
		OLED_ShowString(0, 2*8, "S2  LR   UD  BTN", OLED_6X8);
		OLED_ShowString(0, 5*8, "SIM:", OLED_6X8);
		//OLED_ShowString(0, 7*8, "x y z yw=", OLED_6X8);
	}

	while(1)
    {
		// USB Touch Deal - 1ms 轮询
		if(tick_ms - touch_deal_time >= 1)
		{
			touch_deal_time = tick_ms;
			Touch_Deal();
		}

		// SIM (MQTT) 初始化
		if((tick_ms/1000)>=20 && sim_lock_1 != 127)
		{
			if(sim_lock_1 == 0)
			{
				SIM_SendCmd("ATI");
				sim_lock_1 = 1;
			}
			if((tick_ms/1000)>=22 && sim_lock_1 == 1)
			{
				// SIM_SendCmd("AT+NETCLOSE");     // 上电先断开数据网络（防止ERROR: 902，虽然说无害）
				SIM_SendCmd("AT+NETOPEN");
				sim_lock_1 = 2;
			}
			if((tick_ms/1000)>=24 && sim_lock_1 == 2)
			{
				SIM_SendCmd("AT+CEREG?");
				SIM_SendCmd("AT+MDISCONNECT");  // 上电先断开MQTT连接（防止重复连接）
				SIM_SendCmd("AT+MIPCLOSE");     // 释放MQTT资源（标准操作，先断MQTT连接再释放资源）
				sim_lock_1 = 3;
			}
			if((tick_ms/1000)>=26 && sim_lock_1 == 3)
			{
				SIM_SendCmd("AT+MCONFIG=ct511test1,ct511test1,ct511test1");
				sim_lock_1 = 4;
			}
			if((tick_ms/1000)>=28 && sim_lock_1 == 4)
			{
				SIM_SendCmd("AT+MIPSTART=47.108.232.40,1883");
				sim_lock_1 = 5;
			}
			if((tick_ms/1000)>=30 && sim_lock_1 == 5)
			{
				SIM_SendCmd("AT+MCONNECT=1,60");
				sim_lock_1 = 127;
			}
		}

		// 读SIM AT应答
   		while((line_len = SIM_GetLine(line_buf, sizeof(line_buf))) > 0)
		{
			// 显示到OLED（覆盖上一行）
			OLED_ClearArea(0,6*8,128,8);  // 清行，防止上一次数据多，这一次数据少，显示混杂
			OLED_ShowString(0, 6*8, (char*)line_buf, OLED_6X8);
			printf("[Raw AT] %s\r\n", line_buf);

			if(strstr((char*)line_buf, "+NETOPEN:SUCCESS"))    {printf("[SIM] 4G READY!\r\n");}
			/*
			// 示例：判断网络是否打开
			if(strstr((char*)line_buf, "+NETOPEN:SUCCESS"))    {printf("[SIM] 4G READY!\r\n");}
			else if(strstr((char*)line_buf, "+NETOPEN:FAIL"))  {printf("[SIM] 4G FAILED!\r\n");}
			else if(strstr((char*)line_buf, "+CEREG: 0,1"))    {printf("[SIM] 4G LINK OK!\r\n");}
			*/
    	}

		/*
		// 测试发布消息 (for debug)
		if(sim_lock_1 == 127 && (tick_ms%5000) == 0)
		{
			SIM_SendCmd("AT+MQTTSTATU");
            SIM_SendCmd("AT+MPUB=\"hello\",0,0,\"Hello message from CT-511, 4G module~~\"");
		}
		*/

		// OLED显示 (for debug)
		if(tick_ms%100==0)
		{
			OLED_ShowNum(3*6, 1*8, Stick1.x, 4, OLED_6X8);
			OLED_ShowNum(8*6, 1*8, Stick1.y, 4, OLED_6X8);
			OLED_ShowNum(14*6, 1*8, Stick1.button, 1, OLED_6X8);

			OLED_ShowNum(3*6, 3*8, Stick2.x, 4, OLED_6X8);
			OLED_ShowNum(8*6, 3*8, Stick2.y, 4, OLED_6X8);
			OLED_ShowNum(14*6, 3*8, Stick2.button, 1, OLED_6X8);

			OLED_ShowNum(0, 4*8, tick_ms/1000, 6, OLED_6X8);
			OLED_ShowChar(6*6, 4*8, '.', OLED_6X8);
			OLED_ShowNum(7*6, 4*8, tick_ms/100, 1, OLED_6X8);
			OLED_ShowChar(8*6, 4*8, 's', OLED_6X8);

			OLED_ShowSignedNum(0, 7*8, rc_packet.x, 2, OLED_6X8);
			OLED_ShowSignedNum(4*6, 7*8, rc_packet.y, 2, OLED_6X8);
			OLED_ShowSignedNum(8*6, 7*8, rc_packet.z, 2, OLED_6X8);
			OLED_ShowSignedNum(12*6, 7*8, rc_packet.yaw, 2, OLED_6X8);
			OLED_Update();
		}
	}
}
