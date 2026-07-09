#ifndef __CONTROL_H
#define __CONTROL_H

#include <stdint.h>
#include <stdbool.h>

// 推进器编号（与 PWM 通道对应）
#define THRUSTER_T1 0  // 左前
#define THRUSTER_T2 1  // 右前
#define THRUSTER_T3 2  // 左后
#define THRUSTER_T4 3  // 右后
#define THRUSTER_T5 4  // 左（垂直）
#define THRUSTER_T6 5  // 右（垂直）

// PWM 范围
#define PWM_NEUTRAL_US 1500
#define PWM_MIN_US     1000
#define PWM_MAX_US     2000

// 输入范围（所有轴统一为 ±50）
#define INPUT_MAX_XY   50
#define INPUT_MAX_Z    50
#define INPUT_MAX_YAW  50

// [for pid]  深度 PID 参数（可调整）
#define DEPTH_KP 0.3f    // 比例增益
#define DEPTH_KI 0.02f   // 积分增益
#define DEPTH_KD 0.3f    // 微分增益
#define DEPTH_INTEGRAL_LIMIT 0.1f   // 积分限制
#define DEPTH_OUTPUT_LIMIT 1.0f     // PID输出限制

// 函数声明
void Control_Neutral(void);
void Control_Update(int16_t x, int16_t y, int16_t z, int16_t yaw);
void Control_ResetDepthHold(void);   // [for pid] 重置深度保持状态
bool Control_IsInDepthHold(void);    // [for pid] 检查是否在深度保持模式

#endif /* __CONTROL_H */
