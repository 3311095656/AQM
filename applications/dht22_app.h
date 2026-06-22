#ifndef __DHT22_APP_H__
#define __DHT22_APP_H__

#include <rtthread.h>

/* 初始化 DHT22 室内温湿度传感器，成功返回 RT_EOK */
int dht22_app_init(void);

/* 读取室内温度（℃）、湿度（%），返回 1 表示读取成功 */
int dht22_app_read(float *temperature, float *humidity);

#endif
