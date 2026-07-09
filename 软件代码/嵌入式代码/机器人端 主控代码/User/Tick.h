#ifndef __TICK_H
#define __TICK_H

// 初始化TIM6为1ms滴答
void Tick_Init(void);
void Fiber_TimerIRQHandler(void); // TIM6 中断

#endif /* __TICK_H */
