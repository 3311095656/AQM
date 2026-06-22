#ifndef __OLED_APP_H__
#define __OLED_APP_H__

#include <rtthread.h>
#include "sensor_app.h"

/* 初始化 OLED 显示模块，成功返回 RT_EOK */
int oled_app_init(void);

/* 更新 OLED 屏幕上的传感器数据、燃气和人体状态 */
void oled_app_update(const sensor_data_t *data, int gas_ok, int presence);

#endif
