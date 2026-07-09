#include "FuzzyPID.h"

/* 内部函数：梯形隶属度计算 */
static float _membership(float x, float a, float b, float c, float d) {
    if (x <= a || x >= d) return 0.0f;
    else if (x > a && x <= b) return (x - a) / (b - a);
    else if (x > b && x <= c) return 1.0f;
    else return (d - x) / (d - c);
}

/* 内部函数：重心法解模糊 */
static float _defuzzify(float agg[], FuzzySet_t sets[], int num) {
    float numerator = 0.0f;
    float denominator = 0.0f;
    
    for (int i = 0; i < num; i++) {
        // 取隶属度函数中心点 (b+c)/2 作为代表值
        float center = (sets[i].b + sets[i].c) * 0.5f;
        numerator += agg[i] * center;
        denominator += agg[i];
    }
    
    return (denominator < 0.001f) ? 0.0f : (numerator / denominator);
}

/* 初始化模糊集合参数（与Python版本一致） */
static void _init_fuzzy_sets(Fuzzy_Engine_t* fe) {
    // 定义7个模糊集：NB, NM, NS, ZO, PS, PM, PB
    // 参数：[a, b, c, d]
    const float params[7][4] = {
        {-999.0f, -3.0f, -2.0f, -1.0f},   // NB
        {-2.0f,   -1.5f, -1.0f, -0.5f},   // NM
        {-1.0f,   -0.5f,  0.0f,  0.5f},   // NS
        {-0.5f,    0.0f,  0.0f,  0.5f},   // ZO
        { 0.0f,    0.5f,  1.0f,  1.5f},   // PS
        { 0.5f,    1.0f,  1.5f,  2.0f},   // PM
        { 1.0f,    2.0f,  3.0f,  999.0f}  // PB
    };
    
    for (int i = 0; i < 7; i++) {
        fe->e_sets[i].a = params[i][0];
        fe->e_sets[i].b = params[i][1];
        fe->e_sets[i].c = params[i][2];
        fe->e_sets[i].d = params[i][3];
        
        // de和out使用相同论域
        fe->de_sets[i] = fe->e_sets[i];
        fe->out_sets[i] = fe->e_sets[i];
    }
}

/* 初始化模糊规则表（与Python一致） */
static void _init_rules(Fuzzy_Engine_t* fe) {
    // Kp规则表
    const int8_t kp[7][7] = {
        {3, 3, 2, 2, 1, 0, 0},
        {3, 3, 2, 1, 1, 0, -1},
        {2, 2, 2, 1, 0, -1, -1},
        {2, 2, 1, 0, -1, -1, -2},
        {1, 1, 0, -1, -1, -2, -2},
        {0, 0, -1, -1, -2, -2, -3},
        {0, -1, -1, -2, -2, -3, -3}
    };
    // Ki规则表
    const int8_t ki[7][7] = {
        {-3, -3, -2, -2, -1, 0, 0},
        {-3, -3, -2, -1, -1, 0, 1},
        {-2, -2, -2, -1, 0, 1, 1},
        {-2, -2, -1, 0, 1, 1, 2},
        {-1, -1, 0, 1, 1, 2, 2},
        {0, 0, 1, 1, 2, 2, 3},
        {0, 1, 1, 2, 2, 3, 3}
    };
    // Kd规则表
    const int8_t kd[7][7] = {
        {0, 0, 1, 1, 1, 2, 2},
        {0, 1, 1, 1, 2, 2, 3},
        {1, 1, 1, 2, 2, 3, 3},
        {1, 1, 2, 2, 3, 3, 3},
        {1, 2, 2, 3, 3, 3, 3},
        {2, 2, 3, 3, 3, 3, 3},
        {2, 3, 3, 3, 3, 3, 3}
    };
    
    memcpy(fe->Kp_rules, kp, sizeof(kp));
    memcpy(fe->Ki_rules, ki, sizeof(ki));
    memcpy(fe->Kd_rules, kd, sizeof(kd));
}

/* 模糊推理核心 */
static void _fuzzy_inference(Fuzzy_Engine_t* fe, float e, float de, 
                            float* dKp, float* dKi, float* dKd) {
    float e_mem[7], de_mem[7];
    float kp_agg[7] = {0}, ki_agg[7] = {0}, kd_agg[7] = {0};
    
    // 1. 模糊化
    for (int i = 0; i < 7; i++) {
        e_mem[i] = _membership(e, fe->e_sets[i].a, fe->e_sets[i].b, 
                               fe->e_sets[i].c, fe->e_sets[i].d);
        de_mem[i] = _membership(de, fe->de_sets[i].a, fe->de_sets[i].b,
                                fe->de_sets[i].c, fe->de_sets[i].d);
    }
    
    // 2. 规则推理（Mamdani：min激活，max合成）
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            float firing = (e_mem[i] < de_mem[j]) ? e_mem[i] : de_mem[j]; // min
            
            if (firing > 0.001f) {
                // 将-3~+3映射到0~6索引
                int kp_idx = fe->Kp_rules[i][j] + 3;
                int ki_idx = fe->Ki_rules[i][j] + 3;
                int kd_idx = fe->Kd_rules[i][j] + 3;
                
                // 限幅保护
                kp_idx = (kp_idx < 0) ? 0 : (kp_idx > 6) ? 6 : kp_idx;
                ki_idx = (ki_idx < 0) ? 0 : (ki_idx > 6) ? 6 : ki_idx;
                kd_idx = (kd_idx < 0) ? 0 : (kd_idx > 6) ? 6 : kd_idx;
                
                // max合成
                if (firing > kp_agg[kp_idx]) kp_agg[kp_idx] = firing;
                if (firing > ki_agg[ki_idx]) ki_agg[ki_idx] = firing;
                if (firing > kd_agg[kd_idx]) kd_agg[kd_idx] = firing;
            }
        }
    }
    
    // 3. 解模糊
    *dKp = _defuzzify(kp_agg, fe->out_sets, 7);
    *dKi = _defuzzify(ki_agg, fe->out_sets, 7);
    *dKd = _defuzzify(kd_agg, fe->out_sets, 7);
}

/* ========== 对外接口实现 ========== */

void FuzzyPID_Init(FuzzyPID_Controller_t* fpid, float Kp, float Ki, float Kd) {
    memset(fpid, 0, sizeof(FuzzyPID_Controller_t));
    
    fpid->pid.Kp0 = Kp; fpid->pid.Kp = Kp;
    fpid->pid.Ki0 = Ki; fpid->pid.Ki = Ki;
    fpid->pid.Kd0 = Kd; fpid->pid.Kd = Kd;
    
    fpid->pid.integral_max = 100.0f;    // 根据实际系统调整
    fpid->pid.output_max = PID_OUTPUT_MAX;
    
    _init_fuzzy_sets(&fpid->fuzzy);
    _init_rules(&fpid->fuzzy);
}

void FuzzyPID_Reset(FuzzyPID_Controller_t* fpid) {
    fpid->pid.prev_error = 0.0f;
    fpid->pid.integral = 0.0f;
    fpid->pid.Kp = fpid->pid.Kp0;
    fpid->pid.Ki = fpid->pid.Ki0;
    fpid->pid.Kd = fpid->pid.Kd0;
}

float FuzzyPID_Update(FuzzyPID_Controller_t* fpid, float setpoint, float current, float dt) {
    PID_Core_t* pid = &fpid->pid;
    
    // 计算误差和误差变化率
    float error = setpoint - current;
    float delta_error = error - pid->prev_error;
    
    // 量化到模糊论域 [-3, +3]
    float e_quant = error * FUZZY_SCALE_E;
    float de_quant = delta_error * FUZZY_SCALE_DE;
    
    // 限幅
    if (e_quant > 3.0f) e_quant = 3.0f;
    if (e_quant < -3.0f) e_quant = -3.0f;
    if (de_quant > 3.0f) de_quant = 3.0f;
    if (de_quant < -3.0f) de_quant = -3.0f;
    
    // 模糊推理得到参数调整量
    _fuzzy_inference(&fpid->fuzzy, e_quant, de_quant, 
                    &fpid->delta_Kp, &fpid->delta_Ki, &fpid->delta_Kd);
    
    // 更新PID参数（带积分式限幅，防止参数漂移）
    pid->Kp += fpid->delta_Kp * 0.05f;  // 0.05为学习率，可调
    pid->Ki += fpid->delta_Ki * 0.02f;
    pid->Kd += fpid->delta_Kd * 0.05f;
    
    // 参数限幅（非负且合理范围）
    if (pid->Kp < 0.0f) pid->Kp = 0.0f;
    if (pid->Ki < 0.0f) pid->Ki = 0.0f;
    if (pid->Kd < 0.0f) pid->Kd = 0.0f;
    if (pid->Kp > 10.0f) pid->Kp = 10.0f;
    if (pid->Ki > 5.0f) pid->Ki = 5.0f;
    if (pid->Kd > 5.0f) pid->Kd = 5.0f;
    
    // PID计算
    pid->integral += error * dt;
    if (pid->integral > pid->integral_max) pid->integral = pid->integral_max;
    if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;
    
    float derivative = delta_error / dt;
    float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;
    
    // 输出限幅
    if (output > pid->output_max) output = pid->output_max;
    if (output < -pid->output_max) output = -pid->output_max;
    
    pid->prev_error = error;
    fpid->last_output = output;
    
    return output;
}

void FuzzyPID_GetStatus(FuzzyPID_Controller_t* fpid, float* curr_Kp, float* curr_Ki, float* curr_Kd, float* err) {
    *curr_Kp = fpid->pid.Kp;
    *curr_Ki = fpid->pid.Ki;
    *curr_Kd = fpid->pid.Kd;
    *err = fpid->pid.prev_error;
}