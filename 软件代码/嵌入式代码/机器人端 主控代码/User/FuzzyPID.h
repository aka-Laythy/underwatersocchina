#ifndef __FUZZYPID_H
#define __FUZZYPID_H

#include <stdint.h>
#include <math.h>
#include <string.h>

/* 模糊PID配置参数 */
#define FUZZY_SETS_NUM    7
#define FUZZY_SCALE_E     0.5f    // 误差量化因子（根据传感器量程调整）
#define FUZZY_SCALE_DE    1.0f    // 误差变化率量化因子
#define PID_OUTPUT_MAX    500.0f  // 对应PWM偏差 ±500us (1000-2000范围)

/* 模糊集合结构 */
typedef struct {
    float a, b, c, d;  // 梯形隶属度参数：左0点、左1点、右1点、右0点
} FuzzySet_t;

/* PID控制器结构 */
typedef struct {
    float Kp, Ki, Kd;           // 当前参数（运行时自适应调整）
    float Kp0, Ki0, Kd0;        // 初始参数
    float prev_error;           // 上次误差
    float integral;             // 积分累积
    float integral_max;         // 积分限幅（防饱和）
    float output_max;           // 输出限幅
} PID_Core_t;

/* 模糊控制器结构 */
typedef struct {
    FuzzySet_t e_sets[FUZZY_SETS_NUM];      // 误差E的隶属度
    FuzzySet_t de_sets[FUZZY_SETS_NUM];     // 误差变化率dE的隶属度
    FuzzySet_t out_sets[FUZZY_SETS_NUM];    // 输出论域隶属度
    
    // 模糊规则表（7x7），值域-3~+3，对应NB~PB
    int8_t Kp_rules[7][7];
    int8_t Ki_rules[7][7];
    int8_t Kd_rules[7][7];
} Fuzzy_Engine_t;

/* 完整模糊自适应PID结构 */
typedef struct {
    PID_Core_t pid;
    Fuzzy_Engine_t fuzzy;
    float delta_Kp, delta_Ki, delta_Kd;     // 本次模糊输出（调参增量）
    float last_output;                      // 上次输出（用于调试）
} FuzzyPID_Controller_t;

/* 对外接口 */
void FuzzyPID_Init(FuzzyPID_Controller_t* fpid, float Kp, float Ki, float Kd);
void FuzzyPID_Reset(FuzzyPID_Controller_t* fpid);
float FuzzyPID_Update(FuzzyPID_Controller_t* fpid, float setpoint, float current, float dt);

/* 调试接口：获取内部状态 */
void FuzzyPID_GetStatus(FuzzyPID_Controller_t* fpid, float* curr_Kp, float* curr_Ki, float* curr_Kd, float* err);

#endif