#ifndef __PWRBOARD_H
#define __PWRBOARD_H

typedef struct {
    volatile u32 vbus_mA;      // VBUS电流 mA
    volatile u32 bus_5v_mA;    // 5V总线电流 mA  
    volatile u32 bus_12v_mA;   // 12V总线电流 mA
    volatile u8  updated;      // 数据更新标志，接收到新帧后置1，读取后建议清0
} PWR_Monitor_t;

void Pwr_Conn_Init(void);

#endif /* __PWRBOARD_H */
