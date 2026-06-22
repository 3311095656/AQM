#ifndef __SENSOR_APP_H__
#define __SENSOR_APP_H__

#include <rtthread.h>

/* 传感器数据结构体，存放室内外温湿度和光照强度 */
typedef struct {
    float out_temperature;  /* 室外温度，单位：摄氏度（AHT10） */
    float out_humidity;     /* 室外湿度，单位：百分比 %（AHT10） */
    float brightness;       /* 光照强度，单位：勒克斯 lux（AP3216C） */
    float in_temperature;   /* 室内温度，单位：摄氏度（DHT22） */
    float in_humidity;      /* 室内湿度，单位：百分比 %（DHT22） */
    int   window_state;     /* 窗户状态：1=开，0=关 */
    int   presence;         /* 人体存在：1=有人，0=无人 */
} sensor_data_t;

/* 初始化传感器模块，成功返回 RT_EOK */
int sensor_app_init(void);

/* 读取所有传感器数据并存入 data 结构体 */
void sensor_app_read(sensor_data_t *data);

#endif
