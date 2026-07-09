#include "debug.h"
#include "PWM.h"
// #include "Control.h"  // 仅在Fiber模块调用
#include "Fiber.h"
#include "I2C.h"
#include "JY61P.h"
#include "MMC5603.h"
#include "MS5837.h"
#include "Tick.h"
#include "GPS.h"
#include "SPI_Flash.h"
#include "LinuxConnect.h"
#include "SysFlow.h"
#include "PwrBoard.h"
#include <math.h>
#include "FuzzyPID.h"   // 新增模糊PID
/* ========== 控制轴定义 ========== */
typedef enum {
    CTRL_DEPTH = 0,     // 深度控制（垂推4,5）
    CTRL_ROLL,          // 横滚控制（垂推4,5差动）
    CTRL_PITCH,         // 俯仰控制（水平推0-3差动）
    CTRL_YAW,           // 航向控制（水平推0-3差动）
    CTRL_SURGE,         // 前进后退（水平推0-3同步）
    CTRL_SWAY,          // 横移（水平推0-3差动）
    CTRL_NUM
} ControlAxis_t;

/* 6轴模糊PID控制器实例 */
static FuzzyPID_Controller_t g_controllers[CTRL_NUM];

/* 传感器数据结构（根据你的实际传感器填写） */
typedef struct {
    float depth;        // 深度计 (m)
    float roll;         // 横滚角 (°)
    float pitch;        // 俯仰角 (°)
    float yaw;          // 航向角 (°)
    float surge_vel;    // 前向速度 (可选)
    float sway_vel;     // 横向速度 (可选)
} ROV_State_t;

static ROV_State_t g_state = {0};
static ROV_State_t g_target = {0.5f, 0, 0, 0, 0, 0};  // 目标：0.5m深，姿态水平

void Thrust_Mixing(float surge, float sway, float heave, 
                  float roll, float pitch, float yaw,
                  uint16_t* pwm_out) {
    
    const uint16_t NEUTRAL = 1500;  // 1.5ms停转
    
    /* 水平推进器解算（基于45度内八/外八布局）
     * 
     * 符号约定：
     * - surge: +向前，-向后
     * - sway:  +向右，-向左  
     * - yaw:   +顺时针（右转），-逆时针（左转）
     * - pitch: +抬头（船首向上），-低头
     * 
     * 各推进器贡献（需要根据实际测试调整符号）：
     * T0(左前):  contributes +Surge(0.707) -Sway(0.707) -Yaw(1.0) +Pitch(1.0)
     * T1(左后):  contributes -Surge(0.707) +Sway(0.707) -Yaw(1.0) -Pitch(1.0)  
     * T2(右前):  contributes +Surge(0.707) +Sway(0.707) +Yaw(1.0) +Pitch(1.0)
     * T3(右后):  contributes -Surge(0.707) -Sway(0.707) +Yaw(1.0) -Pitch(1.0)
     */
    
    float T0 = 0.707f * surge - 0.707f * sway - 1.0f * yaw + 1.0f * pitch;
    float T1 = -0.707f * surge + 0.707f * sway - 1.0f * yaw - 1.0f * pitch;
    float T2 = 0.707f * surge + 0.707f * sway + 1.0f * yaw + 1.0f * pitch;
    float T3 = -0.707f * surge - 0.707f * sway + 1.0f * yaw - 1.0f * pitch;
    
    /* 垂直推进器解算
     * T4(左垂): heave + roll（左垂推上浮产生右倾力矩）
     * T5(右垂): heave - roll
     */
    float T4 = heave + roll;
    float T5 = heave - roll;
    
    /* 映射到PWM (假设输出±500对应±100%推力) */
    pwm_out[0] = NEUTRAL + (int16_t)(T0 * 5.0f);
    pwm_out[1] = NEUTRAL + (int16_t)(T1 * 5.0f);
    pwm_out[2] = NEUTRAL + (int16_t)(T2 * 5.0f);
    pwm_out[3] = NEUTRAL + (int16_t)(T3 * 5.0f);
    pwm_out[4] = NEUTRAL + (int16_t)(T4 * 5.0f);
    pwm_out[5] = NEUTRAL + (int16_t)(T5 * 5.0f);
    
    /* 硬限幅确保不超出1000-2000us */
    for (int i = 0; i < 6; i++) {
        if (pwm_out[i] < 1000) pwm_out[i] = 1000;
        if (pwm_out[i] > 2000) pwm_out[i] = 2000;
    }
}

/* ========== 控制系统初始化 ========== */
void ROV_ControlSystem_Init(void) {
    // 1. 初始化硬件PWM（你的原有函数）
    PWM_Init();
    
    // 2. 初始化各轴模糊PID（参数需根据实际调试调整）
    // 深度控制：响应慢，允许较大积分
    FuzzyPID_Init(&g_controllers[CTRL_DEPTH], 3.0f, 0.8f, 2.0f);
    
    // 姿态控制：响应快，微分抑制超调，积分防漂移
    FuzzyPID_Init(&g_controllers[CTRL_ROLL], 2.0f, 0.2f, 1.5f);
    FuzzyPID_Init(&g_controllers[CTRL_PITCH], 2.0f, 0.2f, 1.5f);
    FuzzyPID_Init(&g_controllers[CTRL_YAW], 2.5f, 0.1f, 1.0f);
    
    // 运动控制：速度环，积分用于消除稳态误差
    FuzzyPID_Init(&g_controllers[CTRL_SURGE], 1.0f, 0.5f, 0.8f);
    FuzzyPID_Init(&g_controllers[CTRL_SWAY], 1.0f, 0.5f, 0.8f);
    
    printf("[FuzzyPID] Control System Initialized.\r\n");
}

/* ========== 控制周期函数（在定时器中断中调用，建议10ms周期） ========== */
void ROV_Control_Cycle(float dt) {
    uint16_t pwm_values[6];
    
    /* 1. 读取传感器（替换为你的实际驱动） */
    // g_state.depth = MS5837_ReadDepth();
    // g_state.roll = IMU_GetRoll();
    // g_state.pitch = IMU_GetPitch();
    // g_state.yaw = IMU_GetYaw();
    
    /* 2. 计算各轴PID输出（模糊自适应） */
    float heave = FuzzyPID_Update(&g_controllers[CTRL_DEPTH], 
                                  g_target.depth, g_state.depth, dt);
    
    float roll_torque = FuzzyPID_Update(&g_controllers[CTRL_ROLL], 
                                        g_target.roll, g_state.roll, dt);
                                        
    float pitch_torque = FuzzyPID_Update(&g_controllers[CTRL_PITCH], 
                                         g_target.pitch, g_state.pitch, dt);
                                         
    float yaw_torque = FuzzyPID_Update(&g_controllers[CTRL_YAW], 
                                       g_target.yaw, g_state.yaw, dt);
    
    // Surge/Sway可以是遥控输入或路径规划输出
    float surge_cmd = 0.0f;  // 替换为遥控前向通道
    float sway_cmd = 0.0f;   // 替换为遥控横向通道
    
    /* 3. 推进器混控 */
    Thrust_Mixing(surge_cmd, sway_cmd, heave, 
                  roll_torque, pitch_torque, yaw_torque, 
                  pwm_values);
    
    /* 4. 输出到PWM（调用你的原有驱动） */
    for (int i = 0; i < 6; i++) {
        PWM_SetDuty_us(i, pwm_values[i]);
    }
    
    /* 5. 调试信息打印（每100次循环打印一次，避免串口阻塞） */
    static int debug_cnt = 0;
    if (++debug_cnt >= 100) {
        debug_cnt = 0;
        float kp, ki, kd, err;
        FuzzyPID_GetStatus(&g_controllers[CTRL_DEPTH], &kp, &ki, &kd, &err);
        printf("D:%.2f T:%.2f H:%.1f Kp:%.2f\r\n", 
               g_state.depth, g_target.depth, heave, kp);
    }
}

/* ========== 安全保护函数 ========== */
void ROV_Emergency_Stop(void) {
    for (int i = 0; i < 6; i++) {
        PWM_SetDuty_us(i, 1500);  // 全部停转
    }
    for (int i = 0; i < CTRL_NUM; i++) {
        FuzzyPID_Reset(&g_controllers[i]);
    }
}

// 关键时间戳变量
volatile u8 should_feed_dog = 1;        // 什么时候想让单片机复位，就把这个变量置0
extern u32 tick;                        // 全局毫秒时间戳, from tick.c

// 机器人总控变量
extern volatile ROBOTSYSTEM_STATE robotsystem_state;
extern volatile ROBOTMOTION_STATE robotmotion_state;

// 下发航点
extern Linux_Task_Application_Packet task_application_pkt;
// 当前已完成航点号
extern u32 current_task_point_finished;

// 主循环调度变量
volatile u8 main_loop_1s = 0;
static uint32_t last_gps = 0;
static u32 in_self_check_in_water_robot = 0;

// debug 所用变量
extern u8 jedec_id[3];
extern volatile uint8_t rx_buffer[6];
extern int16_t pwm_debug_info[6];
extern volatile PWR_Monitor_t g_pwr;

// GPS 经纬度和日期时间
extern GPS_Coord_TypeDef gps_coord;
extern GPS_Time_TypeDef gps_time;
extern char gps_latest_frame[GPS_FRAME_BUF_SIZE];
extern uint16_t gps_latest_frame_len;

// 传感器读数状态机
typedef enum    // MMC5603
{
    MMC5603_STATE_IDLE = 0,
    MMC5603_STATE_MEASURING
} MMC5603_STATE;
static MMC5603_STATE mmc5603_state = MMC5603_STATE_IDLE;
static u32           mmc5603_state_tick = 0;

typedef enum    // MS5837
{
    MS5837_STATE_IDLE = 0,
    MS5837_STATE_MEASURING_TEMP,
    MS5837_STATE_MEASURING_MAG_AND_CALC
} MS5837_STATE;
static MS5837_STATE ms5837_state = MS5837_STATE_IDLE;
static u32          ms5837_state_tick = 0;
u32 D1, D2;
extern MS5837_Data_t sensor_data;

// CH32V307 demo改
void Essentials(void)
{
    //IWDG_ReloadCounter();       // 注意！这句话必须加在整个程序的最开头。在上次IWDG复位后，必须立刻计数清零？不清楚，反正这样能跑 [260129注：并不需要]
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(375000);
    printf("CH32V307 SystemClk:%u Hz\r\n", SystemCoreClock);
}

// 启用独立看门狗 IWDG
void IWDG_Init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);  // 允许写入
    IWDG_SetPrescaler(IWDG_Prescaler_256);         // 40kHz / 256 = 156.25Hz
    IWDG_SetReload(313);                           // 设置看门狗超时时间2s，156.25Hz*2s~313
    IWDG_ReloadCounter();                          // 首次喂狗
    IWDG_Enable();                                 // 启用看门狗
}

// 总是推进任务流 - 为1启用。不管机器人是否真的准备好了，总是推进任务流。比如：ROBOTSYSTEM_TASK_RECEIVED --立即推进任务流--> ROBOTSYSTEM_IN_WATER
// 危险！
// 仅供调试用
static u8 always_advance_flow = 1;          // 总任务调度
static u8 always_advance_flow_motion = 1;   // 运动调度

// 使能
static u8 ms5837_enable = 1;
static u8 mmc5603_enable = 1;
static u8 jy61p_enable = 1;
static u8 gps_enable = 1;
static u8 spiflash_enable = 1;
static u8 linuxconnect_enable = 1;
static u8 powerconnect_enable = 1;
static u8 rov_control_printf_debug = 1;

int main(void)
{
    Essentials();
    IWDG_Init();
    ROV_ControlSystem_Init();
    Tick_Init();
    PWM_Init();
    Fiber_Init();
    
    Delay_Us(100);
    //IIC2_Scan_Soft();
    IIC2_Recovery(400000);  // 可用recovery直接代替init
    IIC1_Recovery(400000);
    Delay_Ms(50);
    if(ms5837_enable || mmc5603_enable)
    {
        IIC2_Scan();
        IIC1_Scan();
    }

    if(ms5837_enable)   {MS5837_Init();}
    if(mmc5603_enable)  {MMC5603_Init();}
    if(jy61p_enable)    {JY61P_Init();}
    if(gps_enable)      {GPS_Init();}
    if(spiflash_enable) {SPI2_Init(); W25Q512_ReadJEDEC_ID();}
    if(linuxconnect_enable) {LinuxConnect_Init();}
    if(powerconnect_enable) {Pwr_Conn_Init();}
    robotsystem_state = ROBOTSYSTEM_ESSENTIAL_PERI_INIT_FINISHED;

    Delay_Ms(50);
    // 这里后期可以加双向通讯验证等
    robotsystem_state = ROBOTSYSTEM_WAITING_TASK_ISSUE;
    //Fiber_DisableTimeout();  // for debug, 禁用超时
    while(1)
    {
        ROV_Control_Cycle(0.01f);//pid智能运动
        // MMC5603传感器
        if(mmc5603_enable)
        {
            switch(mmc5603_state)
            {
                case MMC5603_STATE_IDLE:
                {
                    if(tick % 20 == 0)
                    {
                        MMC5603_StartMeasurement();
                        mmc5603_state_tick = tick;
                        mmc5603_state = MMC5603_STATE_MEASURING;
                    }
                    break;
                }
                case MMC5603_STATE_MEASURING:
                {
                    if(tick - mmc5603_state_tick >= 15)
                    // 单次6.6ms，但开启Auto_SR后，芯片需执行SET测量+RESET测量（两次转换），总时间约为2*6.6ms=13.2ms，保守给15ms
                    {
                        MMC5603_ReadData(&mag_sensor_data);
                        mag_sensor_data.total_mgauss = sqrt(
                            mag_sensor_data.x_mgauss*mag_sensor_data.x_mgauss + 
                            mag_sensor_data.y_mgauss*mag_sensor_data.y_mgauss +
                            mag_sensor_data.z_mgauss*mag_sensor_data.z_mgauss);
                        if(mag_sensor_data.total_mgauss>1000) {mag_sensor_data.valid = 0;}          // 地磁合场强（毫高斯）>1000，数据无效
                        else {mag_sensor_data.valid = 1;}                                           // 否则有效
                        mmc5603_state = MMC5603_STATE_IDLE;
                    }
                    break;
                }
            }
        }

        // MS5837传感器
        if(ms5837_enable)
        {
            switch(ms5837_state)
            {
                case MS5837_STATE_IDLE:     // 改自 int MS5837_Read(MS5837_Data_t *data) 函数
                {
                    if(tick % 100 == 0)
                    {
                        MS5837_StartTemperatureConversion(CMD_MS5837_CONVERT_D2_4096);
                        ms5837_state_tick = tick;
                        ms5837_state = MS5837_STATE_MEASURING_TEMP;
                    }
                    break;
                } 
                case MS5837_STATE_MEASURING_TEMP:
                {
                    if(tick - ms5837_state_tick >= 10)
                    {
                        D2 = MS5837_ReadADC();
                        MS5837_StartPressureConversion(CMD_MS5837_CONVERT_D1_4096);
                        ms5837_state_tick = tick;
                        ms5837_state = MS5837_STATE_MEASURING_MAG_AND_CALC;
                    }
                    break;
                }
                case MS5837_STATE_MEASURING_MAG_AND_CALC: 
                {
                    if(tick - ms5837_state_tick >= 10)
                    {
                        D1 = MS5837_ReadADC();
                        float temperature_c = MS5837_CalculateTemperature(D2, &g_calibration);
                        float pressure_mbar = MS5837_CalculatePressure(D1, D2, &g_calibration);
                        float depth_mm = MS5837_CalculateDepth(pressure_mbar);
                        sensor_data.temperature_mdegC = (int32_t)(temperature_c * 1000.0f);  // 摄氏度转毫摄氏度
                        sensor_data.pressure_mbar = (int32_t)pressure_mbar;                  // 直接取整数部分
                        sensor_data.depth_mm = (int32_t)depth_mm;                            // 直接取整数部分
                        if(sensor_data.depth_mm<0.0 || sensor_data.depth_mm>100*1000) {sensor_data.valid = 0;}  // 深度（水下为正）<0或者大于100米，数据无效
                        else {sensor_data.valid = 1;}                                                           // 否则有效
                        ms5837_state = MS5837_STATE_IDLE;
                    }
                    break;
                }
            }
        }

        // GPS
        if (tick - last_gps >= 100)
        {
            last_gps += 100;
            GPS_CheckNewFrame();
        }

        // 机器人总任务流状态机推进
        switch(robotsystem_state)
        {
            // 接收linux下传串口航点数据包
            case ROBOTSYSTEM_WAITING_TASK_ISSUE:
            {
                if(LinuxConnect_GetTaskPacket(&task_application_pkt))
                {
                    // printf("[Linux Connect]>>> Received Points: %d, \r\n", task_application_pkt.point_num);
                    printf("[Linux Connect]>>> Received Points: %d, Header:0x%02X, CRC:0x%02X\r\n", 
                    task_application_pkt.point_num,
                    task_application_pkt.header,
                    task_application_pkt.crc8);
            
                    // 打印所有航点详情
                    for(int i = 0; i < task_application_pkt.point_num; i++)
                    {
                        // 直接读取 float（自然对齐结构体，安全访问）
                        float lon = task_application_pkt.points[i].lon;
                        float lat = task_application_pkt.points[i].lat;
                        float dep = task_application_pkt.points[i].depth;
                        uint8_t num = task_application_pkt.points[i].num;
                        
                        // 转换为定点整数（避免 printf 不支持 %f）
                        int lon_i = (int)(fabsf(lon) * 1000000.0f + 0.5f);   // .6f精度
                        int lat_i = (int)(fabsf(lat) * 1000000.0f + 0.5f);
                        int dep_i = (int)(fabsf(dep) * 100.0f + 0.5f);       // .2f精度
                        
                        // 处理符号位
                        char lon_s = (lon < 0) ? '-' : ' ';
                        char lat_s = (lat < 0) ? '-' : ' ';
                        char dep_s = (dep < 0) ? '-' : ' ';
                        
                        printf("[Linux Connect]>>> Num:%03d, Lon:%c%d.%06d, Lat:%c%d.%06d, Dep:%c%d.%02d\r\n",
                            num,
                            lon_s, lon_i / 1000000, lon_i % 1000000,
                            lat_s, lat_i / 1000000, lat_i % 1000000,
                            dep_s, dep_i / 100, dep_i % 100);
                    }
                    robotsystem_state = ROBOTSYSTEM_TASK_RECEIVED;
                }
                break;
            }
            // （--推进任务流--> 下水）
            case ROBOTSYSTEM_TASK_RECEIVED:
            {
                if(always_advance_flow) {robotsystem_state = ROBOTSYSTEM_IN_WATER;}
                in_self_check_in_water_robot = tick;
                break;
            }
            // 下水保持30秒三轴稳定，校准：GPS、IMU、磁角
            case ROBOTSYSTEM_IN_WATER:
            {
                // 【此处写自检、校准代码（非阻塞式）】。
                if(tick - in_self_check_in_water_robot >= 30*1000)
                {
                    robotsystem_state = ROBOTSYSTEM_IN_WATER_SELF_CHECK_FINISHED;
                }
                break;
            }
            // （--推进任务流--> 准备开始任务）
            case ROBOTSYSTEM_IN_WATER_SELF_CHECK_FINISHED:
            {
                if(always_advance_flow) {robotsystem_state = ROBOTSYSTEM_IN_WATER_TASK_READY;}
                break;
            }
            // 准备开始任务，先把新fiber调通，然后加入另一个包：控制包
            // （--推进任务流--> 开始任务）
            case ROBOTSYSTEM_IN_WATER_TASK_READY:
            {
                if(always_advance_flow) {robotsystem_state = ROBOTSYSTEM_TASK_MOVETO_START_POINT;}
                break;
            }
            case ROBOTSYSTEM_TASK_MOVETO_START_POINT:
            {
                // 在水面上移动到start point（第一个点）
                if(MoveToStartPoint())
                {
                    current_task_point_finished = 1;
                    robotsystem_state = ROBOTSYSTEM_TASK_STARTED;
                }
                break;
            }
            case ROBOTSYSTEM_TASK_STARTED:
            {
                // 这个if就让机器人稳稳三轴停住，航向角对正，返回1就继续：diving ready
                if(IsDivingReady()) {robotmotion_state = ROBOTMOTION_ON_SURFACE_DIVING_READY;}
                break;
            }
            case ROBOTSYSTEM_TASK_PAUSED:
            {
                break;
            }
            case ROBOTSYSTEM_TASK_FINISHED_NORMAL:
            {
                break;
            }
            case ROBOTSYSTEM_TASK_FINISHED_ABNORMAL:
            {
                break;
            }
            case ROBOTSYSTEM_ESSENTIAL_PERI_INIT_FINISHED:  // ROBOTSYSTEM_ESSENTIAL_PERI_INIT_FINISHED 主流程已处理
            {
                break;
            }
            case ROBOTSYSTEM_POWER_ON:                      // ROBOTSYSTEM_POWER_ON 不需要处理
            {
                break;
            }
        }

        // 机器人运动学状态机推进
        switch(robotmotion_state)
        {
            case ROBOTMOTION_ON_SURFACE_DIVING_READY:
            {
                // diving ready, 此时航向角已经正确
                if(always_advance_flow_motion) {robotmotion_state = ROBOTMOTION_IN_WATER_DIVING;}
                break;
            }
            case ROBOTMOTION_IN_WATER_DIVING:
            {
                // 下到指定深度
                if(IsDepthReady()) {robotmotion_state = ROBOTMOTION_IN_WATER_MOTION_READY;}
                break;
            }
            case ROBOTMOTION_IN_WATER_MOTION_READY:
            {
                // ROBOTMOTION_IN_WATER_MOTION_READY, 此时深度、航向角已经正确
                if(always_advance_flow_motion) {robotmotion_state = ROBOTMOTION_IN_WATER_DIVING;}
                break;
            }
            case ROBOTMOTION_IN_WATER_IN_MOTION:
            {
                break;
            }
            case ROBOTMOTION_IN_WATER_UPWARD_READY:
            {
                break;
            }
            case ROBOTMOTION_IN_WATER_UPWARD:
            {
                // if(IsUpwardFinished())
                {
                    // 当前所在航点数自增
                    current_task_point_finished++;  
                    robotmotion_state = ROBOTMOTION_ON_SURFACE_CALIBRITION_PENDING;
                }
                break;
            }
            case ROBOTMOTION_ON_SURFACE_CALIBRITION_PENDING:
            {
                break;
            }
            case ROBOTMOTION_PENDING:
            {
                break;
            }
        }

        // 每隔1s进入1次。
        if(main_loop_1s)
        {
            if(should_feed_dog) {IWDG_ReloadCounter();}    // 喂狗 IWDG

            printf("[SYS TICK]>>> %ds\r\n", tick/1000);
            printf("[SYS SCHEDULE]>>> Current System State: %02d\r\n", robotsystem_state);
            printf("[SYS SCHEDULE]>>> Current Motion State: %02d\r\n", robotmotion_state);

            if(powerconnect_enable)
            {
                printf("[Power Board]>>> VBUS:%05dmA, 5V:%04dmA, 12V:%04dmA.\r\n", g_pwr.vbus_mA, g_pwr.bus_5v_mA, g_pwr.bus_12v_mA);
            }

            if(rov_control_printf_debug)
            {
                // 调试，打印当前收到的control xyz yaw值
                Fiber_Debug_Print();
                printf("[CONTROL]>>> T1:%04d, T2:%04d, T3:%04d, T4:%04d, T5:%04d, T6:%04d (PWM LSB)\r\n", 
                        pwm_debug_info[0], pwm_debug_info[1], pwm_debug_info[2], 
                        pwm_debug_info[3], pwm_debug_info[4], pwm_debug_info[5]);
            }

            if(ms5837_enable) {MS5837_Print();}
            if(mmc5603_enable) {MMC5603_Print();}
            if(jy61p_enable) {JY61P_Print();}

            if(gps_enable)
            {
                // printf("[GPS]>>> Frame (%d bytes):\n%s", gps_latest_frame_len, gps_latest_frame);
                GPS_GetCoord();
                printf("[GPS]>>> Coordinate: %d.%06ddeg%c, %d.%06ddeg%c, %s\r\n", 
                    (int)gps_coord.latitude, 
                    (int)((gps_coord.latitude - (int)gps_coord.latitude) * 1000000),
                    gps_coord.lat_dir,
                    (int)gps_coord.longitude,
                    (int)((gps_coord.longitude - (int)gps_coord.longitude) * 1000000),
                    gps_coord.lon_dir,
                    gps_coord.valid ? "VALID" : "INVALID");
                GPS_GetTime();
                printf("[GPS]>>> BJT_Datetime: %04d-%02d-%02d %02d:%02d:%02d\n[GPS]>>> UTC_TimeStamp:%d\r\n",
                    gps_time.year, gps_time.month, gps_time.day, gps_time.hour, gps_time.minute, gps_time.second, gps_time.timestamp);
            }
            
            main_loop_1s = 0;    // 状态机
        }

    }
}
