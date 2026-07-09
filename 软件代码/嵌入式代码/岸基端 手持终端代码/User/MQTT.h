#ifndef __MQTT_H
#define __MQTT_H

#include "ch32v30x.h"
#include <stdint.h>

// MQTT 状态
typedef enum {
    MQTT_STATE_IDLE = 0,        // 空闲
    MQTT_STATE_CONFIG,          // 配置客户端
    MQTT_STATE_CONNECTING,      // 连接中
    MQTT_STATE_CONNECTED,       // 已连接
    MQTT_STATE_PUBLISHING,      // 发布中
    MQTT_STATE_SUBSCRIBING,     // 订阅中
    MQTT_STATE_DISCONNECTING,   // 断开中
    MQTT_STATE_ERROR            // 错误
} MQTT_State_t;

// MQTT 配置
typedef struct {
    const char *client_id;      // 客户端ID（必填）
    const char *username;       // 用户名（可选，设为NULL则不使用）
    const char *password;       // 密码（可选）
    const char *server;         // 服务器地址（域名或IP）
    uint16_t port;              // 端口（默认1883）
    uint16_t keepalive;         // 心跳间隔（秒，30~1800）
} MQTT_Config_t;

// 初始化MQTT模块
void MQTT_Init(const MQTT_Config_t *config);
// 主循环调用：驱动状态机（非阻塞）
void MQTT_Process(void);
// 发布QoS0消息（立即返回，实际发送由MQTT_Process驱动）
uint8_t MQTT_Publish(const char *topic, const char *payload);
// 订阅主题（QoS0）
uint8_t MQTT_Subscribe(const char *topic);
// 连接
void MQTT_Connect(void);
// 断开连接
void MQTT_Disconnect(void);
// 获取当前状态
MQTT_State_t MQTT_GetState(void);
// 获取错误码（0=无错误）
uint8_t MQTT_GetLastError(void);

#endif