//  int16_t x, int16_t y, int16_t z, int16_t yaw
//   -50~+50    -50~+50    -50~+50     -50~+50
// 正: 左平移     前平移      上浮       顺时针转
/*                  完整坐标轴图示
                     +Z (上)
           +Y(前) __    ^
                 | \    |
                    \   |
                      \ |
          +X(左)  <--------·
*/

#include "debug.h"
#include <math.h>
#include <stdint.h>
#include "PWM.h"
#include "MS5837.h"     // 深度PID数据从这里来，调函数

#include "Control.h"

int16_t pwm_debug_info[6] = {0, };
volatile uint32_t pid_ticks_twent_ms = 0;  // uint32 max val =2^32-1 间隔20ms 用完要20天

// 内部函数：限幅
static inline float clamp(float val, float min, float max)
{
    if(val < min) return min;
    if(val > max) return max;
    return val;
}

// 内部函数：映射 [min_in, max_in] → [min_out, max_out]
static inline int16_t map(int16_t in, int16_t in_min, int16_t in_max, int16_t out_min, int16_t out_max)
{
    int32_t result = (int32_t)(in - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return (int16_t)clamp((int16_t)result, out_min, out_max);
}

// 深度控制状态
static struct {
    bool in_depth_hold;      // 是否在深度保持模式
    float target_depth;      // 目标深度（米）
    float integral_error;    // 积分误差
    float last_error;        // 上次误差
    float last_time;         // 上次更新时间（秒）
} depth_control_state = {0};

// 深度 PID 计算
static float depth_pid_compute(float current_depth, float dt)
{
    // 计算误差（目标深度 - 当前深度）
    // 注：深度值越大表示越深，所以上浮需要减小深度
    float error = depth_control_state.target_depth - current_depth;
    
    // 积分项（带抗饱和）
    if (fabsf(error) > 0.15f)        // 15cm 死区
    {
        depth_control_state.integral_error += error * dt;
        depth_control_state.integral_error = clamp(
            depth_control_state.integral_error, 
            -DEPTH_INTEGRAL_LIMIT, 
            DEPTH_INTEGRAL_LIMIT
        );
    }

    // 微分项
    float derivative = 0.0f;
    if (dt > 0.001f) {  // 避免除零
        derivative = (error - depth_control_state.last_error) / dt;
    }
    
    // PID 计算
    float output = DEPTH_KP * error + 
                  DEPTH_KI * depth_control_state.integral_error + 
                  DEPTH_KD * derivative;
    
    // 限制输出范围（-1.0 ~ +1.0）
    output = clamp(output, -DEPTH_OUTPUT_LIMIT, DEPTH_OUTPUT_LIMIT);
    
    // 保存状态
    depth_control_state.last_error = error;
    
    return output;
}

// 公共接口：全部中立（不动）
void Control_Neutral(void)
{
    // 初始所有推进器为中立（1500 us）
    for(int i = 0; i < 6; i++) {PWM_SetDuty_us(i, PWM_NEUTRAL_US);}
    // 退出深度保持模式
    depth_control_state.in_depth_hold = false;
}

// 重置深度保持状态
void Control_ResetDepthHold(void)
{
    depth_control_state.in_depth_hold = false;
    depth_control_state.integral_error = 0.0f;
    depth_control_state.last_error = 0.0f;
}

// 检查是否在深度保持模式
bool Control_IsInDepthHold(void)
{
    return depth_control_state.in_depth_hold;
}

// 公共接口：主控制更新
void Control_Update(int16_t x, int16_t y, int16_t z, int16_t yaw)
{
    static uint32_t last_update_time = 0;

    // 1. 归一化输入（-50~+50 → -1.0~+1.0）
    float norm_x = (float)x / INPUT_MAX_XY;
    float norm_y = (float)y / INPUT_MAX_XY;
    float norm_z = (float)z / INPUT_MAX_Z;      // 现在 z 也是 ±50
    float norm_yaw = (float)yaw / INPUT_MAX_YAW; // 现在 yaw 也是 ±50

    // 限幅
    norm_x = clamp(norm_x, -1.0f, 1.0f);
    norm_y = clamp(norm_y, -1.0f, 1.0f);
    norm_z = clamp(norm_z, -1.0f, 1.0f);
    norm_yaw = clamp(norm_yaw, -1.0f, 1.0f);

    // 2. 深度控制处理
    float depth_hold_z = 0.0f;
    bool is_depth_hold_active = false;
    
    // 获取当前时间（假设系统有滴答计数器）
    uint32_t current_time = pid_ticks_twent_ms;      // 算pid间隔20ms
    float dt = (current_time - last_update_time) / 1000.0f;  // 转换为秒
    last_update_time = current_time;
    
    // 当 z=0 时，进入深度保持模式
    if (z == 0) {
        if (!depth_control_state.in_depth_hold) {
            // 首次进入深度保持模式
            depth_control_state.in_depth_hold = true;
            depth_control_state.target_depth = Get_Depth_Float_Meter();  // 获取当前深度
            depth_control_state.integral_error = 0.0f;
            depth_control_state.last_error = 0.0f;
            depth_control_state.last_time = current_time / 1000.0f;
            
            // 调试输出
            printf("[CONTROL]>>> Depth hold activated. Target: %d.%02dm\n", 
                   (u8)(depth_control_state.target_depth*100)/100, (u8)(depth_control_state.target_depth*100)%100);
        }
        
        // 计算 PID 输出
        float current_depth = Get_Depth_Float_Meter();
        depth_hold_z = depth_pid_compute(current_depth, dt);
        is_depth_hold_active = true;
    } else {
        // 退出深度保持模式
        if (depth_control_state.in_depth_hold) {
            printf("[CONTROL]>>> Depth hold deactivated.\n");
            depth_control_state.in_depth_hold = false;
        }
        depth_hold_z = norm_z;  // 使用用户输入
    }

    // 2. 水平面推力分配（X/Y/Yaw）
    // 注意 - 定义：
    //   - 水平推进器：1.5~2.0ms = 向外推水（正推力）
    //   - 因此：command > 0 → PWM > 1500

    // 下面这个矩阵 20260302 晚上更新
    // 前方两个是内八（左前正推力朝东北，右前正推力朝西北），后面两个是外八（左后正推力朝东南，右后正推力朝西南），都是45度，如正西南。
    // ask ai: https://www.kimi.com/chat/19caee18-b282-8328-8000-09c21d7c5b4e
    float t1 = -norm_x + norm_y + norm_yaw;  // 左前：东北推，产生 -X(右), +Y(前), +Yaw(顺)
    float t2 =  norm_x + norm_y - norm_yaw;  // 右前：西北推，产生 +X(左), +Y(前), -Yaw(逆)
    float t3 = -norm_x - norm_y - norm_yaw;  // 左后：东南推，产生 -X(右), -Y(后), -Yaw(逆)
    float t4 =  norm_x - norm_y + norm_yaw;  // 右后：西南推，产生 +X(左), -Y(后), +Yaw(顺)

    // 3. 垂直推力分配（Z）
    // 您定义：
    //   - Z = +1（向上移动）→ 向下推水 → T5: 1.0~1.5ms, T6: 1.5~2.0ms
    //   - Z = -1（向下移动）→ 向上推水 → T5: 1.5~2.0ms, T6: 1.0~1.5ms
    // 因此：
    //   - T5: command = -z （反向）
    //   - T6: command = -z
    float t5 = depth_hold_z;  // 左推进器【20260115晚上修正，这里应该是正号！深度我给的就是正值，越深，正的越大】
    float t6 = depth_hold_z;  // 右推进器

    // 4. 合并所有命令
    float thrust[6] = {t1, t2, t3, t4, t5, t6};

    // 5. 归一化（防超限）
    float max_cmd = 0.0f;
    for(int i = 0; i < 6; i++) {if(fabsf(thrust[i]) > max_cmd) max_cmd = fabsf(thrust[i]);}
    if(max_cmd > 1.0f) {for(int i = 0; i < 6; i++) thrust[i] /= max_cmd;}

    // 6. 映射到 PWM (1000~2000 us)
    int16_t pwm[6];
    for(int i = 0; i < 6; i++) {
        // thrust[i] ∈ [-1.0, +1.0]
        // 映射到 PWM: -1.0 → 1000us, 0 → 1500us, +1.0 → 2000us
        pwm[i] = map((int16_t)(thrust[i] * 1000), -1000, 1000, PWM_MIN_US, PWM_MAX_US);
        pwm_debug_info[i] = pwm[i];
    }

    // 7. 发送 PWM
    PWM_SetDuty_us(THRUSTER_T1, pwm[0]);
    PWM_SetDuty_us(THRUSTER_T2, pwm[1]);
    PWM_SetDuty_us(THRUSTER_T3, pwm[2]);
    PWM_SetDuty_us(THRUSTER_T4, pwm[3]);
    PWM_SetDuty_us(THRUSTER_T5, pwm[4]);
    PWM_SetDuty_us(THRUSTER_T6, pwm[5]);

    // 9. 调试输出（可选）
    if (is_depth_hold_active) {
    static uint32_t last_debug_time = 0;
    if (current_time - last_debug_time > 1000) {
        float current_depth = Get_Depth_Float_Meter();

        int t = (int)(depth_control_state.target_depth * 100.0f + 0.5f);
        int c = (int)(current_depth               * 100.0f + 0.5f);
        int o = (int)(depth_hold_z                * 100.0f + 0.5f);

        printf("[DEPTH] Target: ");
        if (t < 0) { printf("-"); t = -t; }
        printf("%d.%02d, Current: ", t/100, t%100);

        if (c < 0) { printf("-"); c = -c; }
        printf("%d.%02d, Output: ", c/100, c%100);

        if (o < 0) { printf("-"); o = -o; }
        printf("%d.%02d\n", o/100, o%100);

        last_debug_time = current_time;
    }
}
}

