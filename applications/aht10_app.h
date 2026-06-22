#ifndef __AHT10_APP_H__
#define __AHT10_APP_H__

#include <rtthread.h>

/* 初始化 AHT10 室外温湿度传感器（I2C3），成功返回 RT_EOK */
int aht10_app_init(void);

/* 读取室外温度（℃），返回浮点数 */
float aht10_app_read_temperature(void);

/* 读取室外湿度（%），返回浮点数 */
float aht10_app_read_humidity(void);

#endif
