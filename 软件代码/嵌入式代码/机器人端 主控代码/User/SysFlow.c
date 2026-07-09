#include "debug.h"
#include "LinuxConnect.h"
#include "gps.h"
#include "MMC5603.h"
#include "math.h"
#include "Control.h"
#include "MS5837.h"
#include "JY61P.h"

#include "SysFlow.h"

// 辅助宏：角度归一化到 -180 ~ 180
#define NORMALIZE_ANGLE_180(a)  do { \
    while ((a) > 180.0f) (a) -= 360.0f; \
    while ((a) < -180.0f) (a) += 360.0f; \
} while(0)
// 辅助宏：浮点数限幅
#define CLAMP_FLOAT(val, min, max)  ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

// sys flow defined here. 默认都是0
volatile ROBOTSYSTEM_STATE robotsystem_state = 0;
volatile ROBOTMOTION_STATE robotmotion_state = 0;

// extern GPS
extern GPS_Coord_TypeDef gps_coord;
// 下发航点
extern Linux_Task_Application_Packet task_application_pkt;
// extern "systick"
extern volatile u32 tick;
// 起始点
float start_point_lon = 0, start_point_lat = 0;
// arrived_start_point = 1
u8 arrived_start_point = 0;
// MoveToStartPoint 状态机
u8 move_to_start_point_state = 0;
// "lasttick"
u32 last_tick = 0;
// u32 current_task_point_finished
u32 current_task_point_finished = 0;
// extern imu structure
extern volatile JY61P_Data_t g_imu_data;

u8 MoveToStartPoint(void)
{
    if(move_to_start_point_state == 0)
    {
        start_point_lon = task_application_pkt.points[0].lon;
        start_point_lat = task_application_pkt.points[0].lat;
        move_to_start_point_state = 1;
    }
    if(move_to_start_point_state == 1)
    {
        if(tick - last_tick >= 100)  // 每隔100ms进入
        {
            last_tick = tick;
            if(Nav_Task_Update())    // 移动并判断是否到达
            {
                move_to_start_point_state = 127;
            }
        }
    }

    if(move_to_start_point_state == 127)
    {
        robotsystem_state = ROBOTSYSTEM_TASK_STARTED;
        return 1;
    }
    return 0;
}

/* 计算两点间距离 (Haversine 公式简化版) */
float CalcDistance(float lat1, float lon1, float lat2, float lon2)
{
    float dlat = (lat2 - lat1) * (3.1415926f / 180.0f);
    float dlon = (lon2 - lon1) * (3.1415926f / 180.0f);
    float a = sinf(dlat / 2.0f) * sinf(dlat / 2.0f) + 
              cosf(lat1 * (3.1415926f / 180.0f)) * cosf(lat2 * (3.1415926f / 180.0f)) * 
              sinf(dlon / 2.0f) * sinf(dlon / 2.0f);
    float c = 2.0f * atan2f(sinf(sqrtf(a)), cosf(sqrtf(a)));
    return EARTH_RADIUS_METERS * c;
}

/* 计算目标方位角 (Bearing, 归一化到 0-360 deg) */
float CalcBearing(float lat1, float lon1, float lat2, float lon2)
{
    float dlon = (lon2 - lon1) * (3.1415926f / 180.0f);
    float y = sinf(dlon) * cosf(lat2 * (3.1415926f / 180.0f));
    float x = cosf(lat1 * (3.1415926f / 180.0f)) * sinf(lat2 * (3.1415926f / 180.0f)) - 
              sinf(lat1 * (3.1415926f / 180.0f)) * cosf(lat2 * (3.1415926f / 180.0f)) * cosf(dlon);
    float bearing = atan2f(y, x) * (180.0f / 3.1415926f);
    return (bearing + 360.0f); // 归一化到 0-360
}

/* move to start point 循环更新 (20-50Hz is recommended) */
u8 Nav_Task_Update(void)
{
    float cur_lat, cur_lon;
    float yaw_true;
    int16_t cmd_x = 0, cmd_y = 0, cmd_yaw = 0;

    /* --- 感知层 --- */
    cur_lon = gps_coord.longitude, cur_lat = gps_coord.latitude;
    yaw_true = GetYawTrueNorth();

    /* --- 几何解算 --- */
    float dist = CalcDistance(cur_lat, cur_lon, start_point_lat, start_point_lon);
    
    /* 到达判定 */
    if (dist < NAV_ARRIVAL_RADIUS_M)
    {
        arrived_start_point = 1;
        Control_Update(0, 0, 0, 0); /* 到达后停机或保持位置 */
        return 1; /* 或进入悬停逻辑 */
    }
    arrived_start_point = 0;

    float bearing_true = CalcBearing(cur_lat, cur_lon, start_point_lat, start_point_lon);
    
    /* 计算相对角度：目标在船体的哪个方向 (-180~180) */
    float rel_angle = bearing_true - yaw_true;
    NORMALIZE_ANGLE_180(rel_angle);

    /* --- 控制律解算 --- */
    
    /* 策略：距离映射为基础速度增益 (距离越远推力越大) */
    float speed_gain = dist * NAV_SPEED_GAIN;
    if (speed_gain > NAV_MAX_CMD_GAIN) speed_gain = NAV_MAX_CMD_GAIN;
    
    /* 模式 A：大角度偏差优先旋转 (防止侧移效率低) */
    if (fabsf(rel_angle) > NAV_ALIGN_THRESHOLD_DEG) {
        /* 纯航向修正：使用 P 控制 */
        cmd_yaw = (int16_t)(rel_angle * NAV_YAW_P_GAIN);
        cmd_yaw = (int16_t)CLAMP_FLOAT((float)cmd_yaw, -50, 50);
        cmd_x = 0; 
        cmd_y = 0; /* 大角度时不建议前进，避免侧滑 */
    } 
    /* 模式 B：小角度偏差，矢量合成斜向移动 */
    else {
        /* 航向保持 (小 P 增益，防止与位置环打架) */
        cmd_yaw = (int16_t)(rel_angle * 0.3f); 
        
        /* 矢量分解：将"去目标点"的意图分解为船体 X/Y 轴 */
        /* 注意：您的坐标系 X+=左(Y 轴正向旋转90度)，符合标准三角函数定义 */
        float raw_y = cosf(rel_angle * 3.1415926f / 180.0f) * speed_gain;
        float raw_x = sinf(rel_angle * 3.1415926f / 180.0f) * speed_gain;
        
        cmd_y = (int16_t)CLAMP_FLOAT(raw_y, -50, 50);
        cmd_x = (int16_t)CLAMP_FLOAT(raw_x, -50, 50);
    }

    /* --- 执行层 --- */
    /* 水面弱正浮力：Z 轴保持 0 (中立)，由浮力平衡 */
    Control_Update(cmd_x, cmd_y, 0, cmd_yaw);
    return 0;
}

u8 IsDivingReady(void)
{
    // === 参数配置区 (可根据实测调整) ===
    #define YAW_ALIGN_THRESHOLD_DEG   5.0f        // 航向对齐判定阈值 (度)
    #define YAW_KP                    0.7f        /* 比例增益 (略小于纯P时0.8, approx 0.5~1.2) */
    #define YAW_KD                    0.12f       /* 微分增益 (等效: Kd/dt, dt=50ms) */
    #define CMD_CLAMP(val)            ((val) > 10 ? 10 : ((val) < -10 ? -10 : (val)))  // 限幅到 yaw -10~+10
    // =================================

    if(tick - last_tick >= 50)  // 20hz
    {
        last_tick = tick;
        
        // 1. 边界安全检查：防止数组越界
        // 假设 task_application_pkt.point_count 为总航点数，请根据实际变量名调整
        if (current_task_point_finished + 1 >= task_application_pkt.point_num)
        {
            // 已是最后一个航点或索引异常，视为无需对齐，直接返回就绪
            Control_Update(0, 0, 0, 0);
            return 1;
        }

        // 2. 计算目标航向 & 当前航向
        float yaw_target = CalcBearing(
            task_application_pkt.points[current_task_point_finished].lat, 
            task_application_pkt.points[current_task_point_finished].lon,
            task_application_pkt.points[current_task_point_finished + 1].lat, 
            task_application_pkt.points[current_task_point_finished + 1].lon);
        float yaw_now = GetYawTrueNorth();

        // 3. 计算归一化误差 [-180, 180]
        float yaw_error = yaw_target - yaw_now;
        while (yaw_error > 180.0f)  yaw_error -= 360.0f;
        while (yaw_error < -180.0f) yaw_error += 360.0f;

        // 4. PD 控制计算 (关键改进)
        static float yaw_error_prev = 0.0f;  // 静态变量保存上周期误差
        
        // 计算误差变化率 (微分项)，同样处理360°跳变
        float derivative = yaw_error - yaw_error_prev;
        if (derivative > 180.0f)  derivative -= 360.0f;
        if (derivative < -180.0f) derivative += 360.0f;
        
        // PD 输出 = P项(纠偏) + D项(阻尼)
        float cmd_yaw = YAW_KP * yaw_error + YAW_KD * derivative;
        
        // 更新历史值 (供下一周期使用)
        yaw_error_prev = yaw_error;

        // 5. 状态判断与输出
        if(fabsf(yaw_error) <= YAW_ALIGN_THRESHOLD_DEG)
        {
            //已对齐：输出零指令，靠阻尼自然稳姿
            Control_Update(0, 0, 0, 0);
            return 1;
        }
        else
        {
            // 纠偏中：PD 控制输出
            Control_Update(0, 0, 0, CMD_CLAMP((int16_t)cmd_yaw));
            return 0;
        }
    }
    return 0;
}

u8 IsDepthReady(void)
{
    // ===== 参数配置区 =====
    #define DEPTH_ARRIVAL_THRESHOLD_M           0.15f       // 深度到达判定阈值 (米) 
    #define DEPTH_KP_IS_DEPTRH_READY            1.2f        // 深度环 P 增益 (建议 0.8~2.0) 
    #define DEPTH_KD_IS_DEPTRH_READY            0.08f       // 深度环 D 增益 (阻尼项) 
    
    #define YAW_ARRIVAL_THRESHOLD_DEG           5.0f        // 航向到达判定阈值 (度) 
    #define YAW_KP                              0.7f        // 航向环 P 增益 (复用之前调好的值) 
    #define YAW_KD                              0.12f       // 航向环 D 增益 
    
    #define CMD_CLAMP_IS_DEPTRH_READY(v, limit) ((v) > (limit) ? (limit) : ((v) < -(limit) ? -(limit) : (v)))
    #define DEPTH_CMD_LIMIT                     30          // 垂直电机最大输出 (-30~+30)，预留余量 
    #define YAW_CMD_LIMIT                       30          // 航向电机最大输出 
    
    #define MAX_DESCENT_RATE_MPS                0.3f        // 最大下沉速率限制 (米/秒)，防扰动
    // =====================

    if(tick - last_tick >= 50)  // 20hz
    {
        last_tick = tick;
        // 1. 获取传感器数据
        float current_depth = Get_Depth_Float_Meter();          // 30Hz 更新
        float current_yaw   = GetYawTrueNorth();                // 真北航向接口
        
        // 可选：读取 IMU 原始数据做安全监控 (100Hz 更新) 
        float roll_deg = g_imu_data.roll_deg;
        float pitch_deg = g_imu_data.pitch_deg;
        float gz_dps = g_imu_data.gz_dps;  // Z 轴角速度，检测旋转抖动

        // 2. 深度环 PD 控制
        static float depth_error_prev = 0.0f;
        // static float depth_integral = 0.0f;  // 预留积分项，当前纯 PD
        
        // 误差计算：current - target > 0 表示太深了，需要上浮 (+z) 
        float depth_error = current_depth - task_application_pkt.points[current_task_point_finished].depth;
        
        // 误差变化率 (微分项)
        float depth_derivative = depth_error - depth_error_prev;
        depth_error_prev = depth_error;
        
        // PD 输出 + 速率限幅 (防积分饱和/突变)
        float cmd_z_raw = DEPTH_KP_IS_DEPTRH_READY * depth_error + DEPTH_KD_IS_DEPTRH_READY * depth_derivative;
        
        // 可选：加入下沉速率限制 (需深度微分，简化版用输出限幅替代)
        int16_t cmd_z = CMD_CLAMP_IS_DEPTRH_READY((int16_t)cmd_z_raw, DEPTH_CMD_LIMIT);

        // 3. 航向环 PD 控制 (复用逻辑)
        static float yaw_error_prev = 0.0f;
        
        float target_yaw_deg = CalcBearing(task_application_pkt.points[current_task_point_finished].lat, 
                                        task_application_pkt.points[current_task_point_finished].lon,
                                        task_application_pkt.points[current_task_point_finished + 1].lat, 
                                        task_application_pkt.points[current_task_point_finished + 1].lon);
        float yaw_error = target_yaw_deg - current_yaw;
        /* 归一化到 [-180, 180] */
        while (yaw_error > 180.0f)  yaw_error -= 360.0f;
        while (yaw_error < -180.0f) yaw_error += 360.0f;
        
        // 微分项 + 跳变处理
        float yaw_derivative = yaw_error - yaw_error_prev;
        if (yaw_derivative > 180.0f)  yaw_derivative -= 360.0f;
        if (yaw_derivative < -180.0f) yaw_derivative += 360.0f;
        yaw_error_prev = yaw_error;
        
        float cmd_yaw_raw = YAW_KP * yaw_error + YAW_KD * yaw_derivative;
        int16_t cmd_yaw = CMD_CLAMP_IS_DEPTRH_READY((int16_t)cmd_yaw_raw, YAW_CMD_LIMIT);

        // 4. 姿态安全监控 (利用 IMU 原始数据)
        // 若横滚/俯仰过大，或角速度异常，强制停机保护
        if (fabsf(roll_deg) > 25.0f || fabsf(pitch_deg) > 25.0f || fabsf(gz_dps) > 60.0f) {
            Control_Update(0, 0, 0, 0);  // 紧急停机 
            return 0;  //返回未就绪
        }

        // 5. 执行控制输出 (X/Y 轴保持 0，允许水平漂移) 
        Control_Update(0, 0, cmd_z, cmd_yaw);

        // 6. 到达判定
        if (fabsf(depth_error) <= DEPTH_ARRIVAL_THRESHOLD_M && 
            fabsf(yaw_error) <= YAW_ARRIVAL_THRESHOLD_DEG) {
            // 可选：到达后输出小保持力，或完全停机靠浮力/阻尼稳姿 
            // Control_Update(0, 0, CMD_CLAMP((int16_t)(depth_error * 0.5f), 10), 
            //                CMD_CLAMP((int16_t)(yaw_error * 0.3f), 5));
            Control_Update(0, 0, 0, 0);  // 简化：到达后停机
            return 1;
        }
        
        return 0;
    }
    return 0;
}
