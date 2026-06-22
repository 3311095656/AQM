#ifndef __ONENET_APP_H__
#define __ONENET_APP_H__

#include <rtthread.h>

/* 启动周期性上传线程，每 5 秒向 OneNET 上报一次随机温度数据 */
int onenet_upload_cycle(void);

/* MSH 命令：向 OneNET 上报数值型数据流，用法：onenet_mqtt_publish_digit <数据流ID> <值> */
int onenet_publish_digit(int argc, char **argv);

/* MSH 命令：向 OneNET 上报字符串型数据流，用法：onenet_mqtt_publish_string <数据流ID> <字符串> */
int onenet_publish_string(int argc, char **argv);

/* MSH 命令：注册 OneNET 平台下发命令的响应回调（同时初始化 LED 引脚） */
int onenet_set_cmd_rsp(int argc, char **argv);

#endif
