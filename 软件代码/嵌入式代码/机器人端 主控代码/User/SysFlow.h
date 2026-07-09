#ifndef __SYSFLOW_H
#define __SYSFLOW_H

// 地球半径（米，采用WGS84标准值：6371008.8米）
#define EARTH_RADIUS_METERS 6371008.8f
// 当地磁偏角，自贡市为 -3.2 deg
#define MAGNETIC_DECLINATION    -3.2f
// 导航参数配置
#define NAV_ARRIVAL_RADIUS_M    5.0f        // 到达判定半径 (米)
#define NAV_ALIGN_THRESHOLD_DEG 15.0f       // 航向对齐阈值 (度) 
#define NAV_MAX_CMD_GAIN        50.0f       // 最大控制输出增益 (-50~50)
#define NAV_SPEED_GAIN          0.5f        // 距离->速度 转换系数 (需整定)
#define NAV_YAW_P_GAIN          0.8f        // 航向环 P 增益 

// 机器人全任务调度状态机
typedef enum    // ROBOT SYSTEM
{
    ROBOTSYSTEM_POWER_ON = 0,                        // 通电
    ROBOTSYSTEM_ESSENTIAL_PERI_INIT_FINISHED,        // 基本外设初始化完成（i2c, spi, uart ...）
    ROBOTSYSTEM_WAITING_TASK_ISSUE,                  // 等待上位机下发任务
    ROBOTSYSTEM_TASK_RECEIVED,                       // 收到任务
    ROBOTSYSTEM_IN_WATER,                            // 仅代表已下水，等待自检、矫正传感器数据
    ROBOTSYSTEM_IN_WATER_SELF_CHECK_FINISHED,        // 已下水、自检，但未矫正传感器数据
    ROBOTSYSTEM_IN_WATER_TASK_READY,                 // 已下水、自检、矫正传感器数据，可以开始执行任务
    ROBOTSYSTEM_TASK_MOVETO_START_POINT,             // 正在移动到start point
    ROBOTSYSTEM_TASK_STARTED,                        // 任务开始
    ROBOTSYSTEM_TASK_PAUSED,                         // 任务暂停
    ROBOTSYSTEM_TASK_FINISHED_NORMAL,                // 任务终止（异常）
    ROBOTSYSTEM_TASK_FINISHED_ABNORMAL,              // 任务终止（正常）
} ROBOTSYSTEM_STATE;

// 机器人水中运动调度状态机
typedef enum    // ROBOT MOTION
{
    ROBOTMOTION_PENDING = 0,                         // 不在水中运动
    ROBOTMOTION_ON_SURFACE_CALIBRITION_PENDING,      // 水面悬停，矫正中
    ROBOTMOTION_ON_SURFACE_DIVING_READY,             // 水面悬停，矫正完毕，下水准备好了
    ROBOTMOTION_IN_WATER_DIVING,                     // 已下水，正在下潜
    ROBOTMOTION_IN_WATER_MOTION_READY,               // 到达指定深度，巡航准备好了
    ROBOTMOTION_IN_WATER_IN_MOTION,                  // 正在水下巡航
    ROBOTMOTION_IN_WATER_UPWARD_READY,               // 此次巡航完毕，准备上浮
    ROBOTMOTION_IN_WATER_UPWARD,                     // 上浮中
} ROBOTMOTION_STATE;

u8 MoveToStartPoint(void);
float CalcDistance(float lat1, float lon1, float lat2, float lon2);
float CalcBearing(float lat1, float lon1, float lat2, float lon2);
u8 Nav_Task_Update(void);
u8 IsDivingReady(void);
u8 IsDepthReady(void);

#endif /* __SYSFLOW_H */
