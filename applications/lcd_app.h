#ifndef __LCD_APP_H__
#define __LCD_APP_H__

#include <rtthread.h>
#include "sensor_app.h"

/* 初始化 LCD 显示界面，绘制标题和标签 */
void lcd_app_init(void);

/* 更新 LCD 屏幕上的传感器数据（温度、湿度、光照） */
void lcd_app_update(const sensor_data_t *data);

/* 更新 LCD 屏幕上的 WiFi 和 MQTT 连接状态 */
void lcd_app_update_status(int wifi_ok, int mqtt_ok);

/* 更新 LCD 屏幕上的 自动/手动 模式显示 */
void lcd_app_update_mode(int is_auto);

/* 更新 LCD 屏幕上的日期时间显示（wifi_ok=1 时显示，否则清除） */
void lcd_app_update_datetime(int wifi_ok);

#endif
