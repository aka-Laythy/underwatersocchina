#ifndef __STEPMOTOR_H
#define __STEPMOTOR_H

void motor_42_init(void);
void ir_limit_init(void);
void motor_42_run(u32 steps);
void motor_42_Stop(void);
void motor_42_set_dir(u8 dir);
u8 limit_check_debounce(u16 pin);

#endif

/*
// example:

int main(void)
{
	essentials();
	Clock_Init();
	motor_42_init();
	ir_limit_init();
	Delay_Ms(200);
    step_target = 0xFFFFFFFF;
    motor_42_run(step_target);
	while(1)
    {
		// 频繁调用即可，函数内部自动10ms节流
        if(limit_check_debounce(GPIO_Pin_13) && step_dir == 0)
		//if(GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) == Bit_RESET && step_dir == 0)
		{
            // 近端限位触发，换向
            TIM_Cmd(TIM1, DISABLE);
            Delay_Ms(10);
            step_dir = 1;
            motor_42_set_dir(step_dir);
            TIM_Cmd(TIM1, ENABLE);
			printf("PA12 LOW\r\n");
        }
        if(limit_check_debounce(GPIO_Pin_14) && step_dir == 1)
		//if(GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_14) == Bit_RESET && step_dir == 1)
		{
            // 远端限位触发，换向
            TIM_Cmd(TIM1, DISABLE);
            Delay_Ms(10);
            step_dir = 0;
            motor_42_set_dir(step_dir);
            TIM_Cmd(TIM1, ENABLE);
			printf("PC14 LOW\r\n");
        }
	}
}

*/
