#ifndef __AP3216C_APP_H__
#define __AP3216C_APP_H__

#include <rtthread.h>

/* 初始化 AP3216C 光照传感器（I2C2），成功返回 RT_EOK */
int ap3216c_app_init(void);

/* 读取环境光照强度（lux），返回浮点数 */
float ap3216c_app_read_light(void);

#endif
