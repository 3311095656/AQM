#ifndef __WIFI_APP_H__
#define __WIFI_APP_H__

#include <rtthread.h>

/* 初始化 WiFi 模块：扫描热点、连接路由器、等待网络就绪，成功返回 RT_EOK */
int wifi_app_init(void);

#endif
