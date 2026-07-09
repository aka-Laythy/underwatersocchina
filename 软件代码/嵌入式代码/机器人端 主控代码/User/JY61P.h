#ifndef __JY61P_H
#define __JY61P_H

typedef struct
{
    float yaw_deg;      // 航向角（度）
    float pitch_deg;    // 俯仰角（度）
    float roll_deg;     // 横滚角（度）
    float ax_g;         // X 轴加速度（g）
    float ay_g;         // Y 轴加速度（g）
    float az_g;         // Z 轴加速度（g）
    float gx_dps;       // X 轴角速度（度/秒）
    float gy_dps;       // Y 轴角速度（度/秒）
    float gz_dps;       // Z 轴角速度（度/秒）
} JY61P_Data_t;

extern volatile JY61P_Data_t g_imu_data;  // 添加 volatile 保证中断安全

int JY61P_Init(void);
void JY61P_Print(void); 

#endif  /* __JY61P_H */
