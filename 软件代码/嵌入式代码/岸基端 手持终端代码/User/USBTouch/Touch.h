#ifndef __TOUCH_H
#define __TOUCH_H

void TIM4_Init(uint16_t arr, uint16_t psc);
u8 Touch_Init(void);
void Touch_Deal(void);

#endif