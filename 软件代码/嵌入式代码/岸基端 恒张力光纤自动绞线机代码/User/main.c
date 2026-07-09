#include "debug.h"
#include "Clock.h"
#include "StepMotor.h"
#include "DCMotor.h"

extern volatile u32 tick_ms;

void essentials(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();
	Delay_Init();
	USART_Printf_Init(115200);	
	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
	printf("This is printf example\r\n");
}

int main(void)
{
	essentials();
	Clock_Init();
	motor_42_init();
	ir_limit_init();
    DCMotor_Init();
	Delay_Ms(200);
	while(1)
    {
        ;
	}
}
