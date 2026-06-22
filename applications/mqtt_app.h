#ifndef __MQTT_APP_H__
#define __MQTT_APP_H__

#include <rtthread.h>
#include "sensor_app.h"

/* 初始化 MQTT 模块，连接 OneNET 物联网平台 */
int mqtt_app_init(void);

/* 将传感器数据上报到 OneNET 平台 */
int mqtt_app_publish_sensor(const sensor_data_t *data);

/* 查询 MQTT 是否已连接到 OneNET 平台，已连接返回非零值 */
int mqtt_app_is_connected(void);

/* 查询是否被远程手动控制（1=手动模式，0=自动模式） */
int mqtt_app_fan_manual(void);
int mqtt_app_window_manual(void);

/* KEY2 切换手动/自动模式，返回当前模式：1=手动，0=自动 */
int mqtt_app_toggle_auto_mode(void);

/* 查询当前是否为自动模式（1=自动，0=手动） */
int mqtt_app_auto_mode(void);

/* 定时计划远程切换自动/手动模式（1=自动，0=手动） */
void mqtt_set_auto_mode_remote(int restore);

#endif
