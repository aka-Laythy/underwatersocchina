#include "string.h"
#include "mqtt.h"
#include "sim.h"
#include "debug.h"

/*
example:
// MQTT配置（静态存储）
static const MQTT_Config_t mqtt_cfg =
{
    .client_id = "ct511test1-1",
    .username = "ct511test1",
    .password = "ct511test1",
    .server = "47.108.232.40",
    .port = 1883,
    .keepalive = 60
};
*/

// 内部状态
static MQTT_State_t mqtt_state = MQTT_STATE_IDLE;
static uint8_t mqtt_error_code = 0;
static uint32_t mqtt_last_tick = 0;      // 上次操作时间戳
static uint32_t mqtt_timeout = 5000;     // 超时5秒
static uint8_t mqtt_retry_count = 0;     // 重试计数
static const MQTT_Config_t *mqtt_config = NULL;

// 待发布/订阅的数据
static char mqtt_pub_topic[64] = {0};
static char mqtt_pub_payload[128] = {0};
static char mqtt_sub_topic[64] = {0};
static uint8_t mqtt_has_pending_pub = 0;
static uint8_t mqtt_has_pending_sub = 0;

// 全局tick_ms变量（由TIM2提供）
extern volatile uint32_t tick_ms;

// 内部函数声明
static void mqtt_send_at_cmd(const char *cmd);
static uint8_t mqtt_check_response(const char *target_ok, const char *target_error);
static void mqtt_set_error(uint8_t code);
static void mqtt_state_transition(MQTT_State_t new_state);

/**
 * @brief 初始化MQTT模块
 * @param config MQTT配置（静态存储，模块不复制内容）
 */
void MQTT_Init(const MQTT_Config_t *config)
{
    if(!config || !config->client_id || !config->server) {
        mqtt_set_error(1); // 无效参数
        return;
    }
    
    mqtt_config = config;
    mqtt_state = MQTT_STATE_IDLE;
    mqtt_error_code = 0;
    mqtt_retry_count = 0;
    mqtt_has_pending_pub = 0;
    mqtt_has_pending_sub = 0;
    
    printf("[MQTT] Initialized. ClientID=%s, Server=%s:%d\r\n",
           config->client_id, config->server, config->port);
}

/**
 * @brief 驱动MQTT状态机（主循环中每10~100ms调用一次）
 */
void MQTT_Process(void)
{
    uint8_t resp_ok = 0;
    uint8_t resp_error = 0;
    uint32_t elapsed = tick_ms - mqtt_last_tick;
    
    switch(mqtt_state) {
        case MQTT_STATE_IDLE:
            // 空闲状态：等待用户调用Publish/Subscribe/Connect
            break;
            
        case MQTT_STATE_CONFIG:
            // 发送MCONFIG配置
            if(elapsed == 0) {
                char cmd[128];
                if(mqtt_config->username && mqtt_config->password) {
                    snprintf(cmd, sizeof(cmd), "AT+MCONFIG=%s,%s,%s",
                            mqtt_config->client_id, mqtt_config->username, mqtt_config->password);
                } else {
                    snprintf(cmd, sizeof(cmd), "AT+MCONFIG=%s", mqtt_config->client_id);
                }
                mqtt_send_at_cmd(cmd);
                mqtt_last_tick = tick_ms;
            } else if(elapsed > mqtt_timeout) {
                mqtt_set_error(2); // 配置超时
                mqtt_state_transition(MQTT_STATE_ERROR);
            } else {
                resp_ok = mqtt_check_response("OK", "+CME ERROR");
                if(resp_ok == 1) {
                    mqtt_state_transition(MQTT_STATE_CONNECTING);
                } else if(resp_ok == 2) {
                    mqtt_set_error(3); // 配置失败
                    mqtt_state_transition(MQTT_STATE_ERROR);
                }
            }
            break;
            
        case MQTT_STATE_CONNECTING:
            // 发送MIPSTART + MCONNECT
            if(elapsed == 0) {
                // 第一步：配置服务器
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "AT+MIPSTART=%s,%d",
                        mqtt_config->server, mqtt_config->port ? mqtt_config->port : 1883);
                mqtt_send_at_cmd(cmd);
                mqtt_last_tick = tick_ms;
            } else if(elapsed < 2000) {
                // 等待MIPSTART响应
                resp_ok = mqtt_check_response("SUCCESS", "FAILURE");
                if(resp_ok == 1) {
                    // 第二步：连接服务器
                    char cmd[64];
                    snprintf(cmd, sizeof(cmd), "AT+MCONNECT=1,%d", 
                            mqtt_config->keepalive ? mqtt_config->keepalive : 60);
                    mqtt_send_at_cmd(cmd);
                    mqtt_last_tick = tick_ms;
                } else if(resp_ok == 2) {
                    mqtt_set_error(4); // 服务器配置失败
                    mqtt_state_transition(MQTT_STATE_ERROR);
                }
            } else if(elapsed < mqtt_timeout) {
                // 等待MCONNECT响应
                resp_ok = mqtt_check_response("SUCCESS", "FAILURE");
                if(resp_ok == 1) {
                    mqtt_state_transition(MQTT_STATE_CONNECTED);
                    printf("[MQTT] Connected to %s\r\n", mqtt_config->server);
                } else if(resp_ok == 2) {
                    mqtt_set_error(5); // 连接失败
                    mqtt_state_transition(MQTT_STATE_ERROR);
                }
            } else {
                mqtt_set_error(6); // 连接超时
                mqtt_state_transition(MQTT_STATE_ERROR);
            }
            break;
            
        case MQTT_STATE_CONNECTED:
            // 检查是否有待发布/订阅任务
            if(mqtt_has_pending_pub) {
                mqtt_state_transition(MQTT_STATE_PUBLISHING);
            } else if(mqtt_has_pending_sub) {
                mqtt_state_transition(MQTT_STATE_SUBSCRIBING);
            }
            // 否则保持连接（模块自动维持keepalive）
            break;
            
        case MQTT_STATE_PUBLISHING:
            if(elapsed == 0) {
                // 发布QoS0消息
                char cmd[256];
                // 转义双引号（payload中可能含"）
                char escaped_payload[130] = {0};
                uint8_t j = 0;
                for(uint8_t i=0; mqtt_pub_payload[i] && i<128; i++) {
                    if(mqtt_pub_payload[i] == '"') {
                        if(j < 128) escaped_payload[j++] = '\\';
                    }
                    if(j < 128) escaped_payload[j++] = mqtt_pub_payload[i];
                }
                
                snprintf(cmd, sizeof(cmd), "AT+MPUB=\"%s\",0,0,\"%s\"",
                        mqtt_pub_topic, escaped_payload);
                mqtt_send_at_cmd(cmd);
                mqtt_last_tick = tick_ms;
                mqtt_has_pending_pub = 0; // 清除任务
            } else if(elapsed > mqtt_timeout) {
                mqtt_set_error(7); // 发布超时
                mqtt_state_transition(MQTT_STATE_CONNECTED);
            } else {
                resp_ok = mqtt_check_response("SUCCESS", "FAILURE");
                if(resp_ok == 1) {
                    printf("[MQTT] Published to %s\r\n", mqtt_pub_topic);
                    mqtt_state_transition(MQTT_STATE_CONNECTED);
                } else if(resp_ok == 2) {
                    mqtt_set_error(8); // 发布失败
                    mqtt_state_transition(MQTT_STATE_CONNECTED);
                }
            }
            break;
            
        case MQTT_STATE_SUBSCRIBING:
            if(elapsed == 0) {
                // 订阅主题（QoS0）
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "AT+MSUB=\"%s\",0", mqtt_sub_topic);
                mqtt_send_at_cmd(cmd);
                mqtt_last_tick = tick_ms;
                mqtt_has_pending_sub = 0; // 清除任务
            } else if(elapsed > mqtt_timeout) {
                mqtt_set_error(9); // 订阅超时
                mqtt_state_transition(MQTT_STATE_CONNECTED);
            } else {
                resp_ok = mqtt_check_response("SUCCESS", "FAILURE");
                if(resp_ok == 1) {
                    printf("[MQTT] Subscribed to %s\r\n", mqtt_sub_topic);
                    mqtt_state_transition(MQTT_STATE_CONNECTED);
                } else if(resp_ok == 2) {
                    mqtt_set_error(10); // 订阅失败
                    mqtt_state_transition(MQTT_STATE_CONNECTED);
                }
            }
            break;
            
        case MQTT_STATE_DISCONNECTING:
            if(elapsed == 0) {
                mqtt_send_at_cmd("AT+MDISCONNECT");
                mqtt_last_tick = tick_ms;
            } else if(elapsed > 2000) {
                mqtt_send_at_cmd("AT+MIPCLOSE");
                mqtt_state_transition(MQTT_STATE_IDLE);
            } else {
                resp_ok = mqtt_check_response("OK", "+CME ERROR");
                if(resp_ok) {
                    mqtt_last_tick = tick_ms; // 继续等待MIPCLOSE
                }
            }
            break;
            
        case MQTT_STATE_ERROR:
            // 错误状态：5秒后自动重置到IDLE
            if(elapsed > 5000) {
                mqtt_state = MQTT_STATE_IDLE;
                mqtt_error_code = 0;
                printf("[MQTT] Error reset after 5s\r\n");
            }
            break;
            
        default:
            mqtt_state = MQTT_STATE_IDLE;
            break;
    }
}

/**
 * @brief 发布QoS0消息（非阻塞）
 * @param topic 主题（最大63字符）
 * @param payload 消息内容（最大127字符）
 * @return 1=成功加入队列, 0=失败（队列满/参数错误）
 */
uint8_t MQTT_Publish(const char *topic, const char *payload)
{
    if(!topic || !payload || mqtt_state != MQTT_STATE_CONNECTED) {
        return 0;
    }
    
    if(strlen(topic) >= 64 || strlen(payload) >= 128) {
        return 0;
    }
    
    strncpy(mqtt_pub_topic, topic, 63);
    mqtt_pub_topic[63] = '\0';
    strncpy(mqtt_pub_payload, payload, 127);
    mqtt_pub_payload[127] = '\0';
    
    mqtt_has_pending_pub = 1;
    return 1;
}

/**
 * @brief 订阅主题（QoS0，非阻塞）
 * @param topic 主题（最大63字符）
 * @return 1=成功加入队列, 0=失败
 */
uint8_t MQTT_Subscribe(const char *topic)
{
    if(!topic || mqtt_state != MQTT_STATE_CONNECTED) {
        return 0;
    }
    
    if(strlen(topic) >= 64) {
        return 0;
    }
    
    strncpy(mqtt_sub_topic, topic, 63);
    mqtt_sub_topic[63] = '\0';
    
    mqtt_has_pending_sub = 1;
    return 1;
}

/**
 * @brief 断开MQTT连接
 */
void MQTT_Disconnect(void)
{
    if(mqtt_state != MQTT_STATE_IDLE && mqtt_state != MQTT_STATE_DISCONNECTING) {
        mqtt_state_transition(MQTT_STATE_DISCONNECTING);
    }
}

/**
 * @brief 连接MQTT服务器（需先确保4G网络已注册）
 */
void MQTT_Connect(void)
{
    if(mqtt_state == MQTT_STATE_IDLE && mqtt_config) {
        mqtt_state_transition(MQTT_STATE_CONFIG);
    }
}

/**
 * @brief 获取当前状态
 */
MQTT_State_t MQTT_GetState(void)
{
    return mqtt_state;
}

/**
 * @brief 获取最后错误码
 */
uint8_t MQTT_GetLastError(void)
{
    return mqtt_error_code;
}

// ============ 内部辅助函数 ============

/**
 * @brief 发送AT指令（通过SIM模块）
 */
static void mqtt_send_at_cmd(const char *cmd)
{
    if(cmd && cmd[0]) {
        SIM_SendCmd(cmd);
        // printf("MQTT TX: %s\r\n", cmd); // 调试用
    }
}

/**
 * @brief 检查响应是否包含目标字符串
 * @param target_ok 目标成功字符串（如"OK"）
 * @param target_error 目标错误字符串（如"+CME ERROR"）
 * @return 0=无匹配, 1=匹配成功, 2=匹配错误
 */
static uint8_t mqtt_check_response(const char *target_ok, const char *target_error)
{
    uint8_t buf[128];
    uint16_t len = SIM_Read(buf, sizeof(buf)-1);
    
    if(len == 0) return 0;
    buf[len] = '\0';
    
    // 转为小写便于匹配（AT响应大小写不敏感）
    char *p = (char*)buf;
    while(*p) {
        if(*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
        p++;
    }
    
    if(strstr((char*)buf, "ok") || (target_ok && strstr((char*)buf, target_ok))) {
        return 1;
    }
    if(strstr((char*)buf, "error") || (target_error && strstr((char*)buf, target_error))) {
        return 2;
    }
    return 0;
}

/**
 * @brief 设置错误码
 */
static void mqtt_set_error(uint8_t code)
{
    mqtt_error_code = code;
    printf("[MQTT] ERROR %d\r\n", code);
}

/**
 * @brief 状态转换（带调试输出）
 */
static void mqtt_state_transition(MQTT_State_t new_state)
{
    // printf("MQTT State: %d -> %d\r\n", mqtt_state, new_state); // 调试用
    mqtt_state = new_state;
    mqtt_last_tick = tick_ms;
    mqtt_retry_count = 0;
}
