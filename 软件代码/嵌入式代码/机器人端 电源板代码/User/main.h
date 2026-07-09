#ifndef __MAIN_H
#define __MAIN_H

void TIM2_Init(uint16_t arr, uint16_t psc);
void USART1_Init(void);
void USART2_Init(void);
void TIM2_IRQHandler(void);
void BLE_Init(void);
void USART2_SendString(uint8_t *str);
void USART2_IRQHandler(void);
void ISense_ADC_Init(void);
u16 ISense_ReadSingleChannel(u8 channel);
void ISense_ADC_Trig(void);
static u32 LowPass_Update(u32 *history, u32 input);
void ISense_Filter_Reset(void);
void ISense_Filter_Apply(void);


#endif /* __MAIN_H */