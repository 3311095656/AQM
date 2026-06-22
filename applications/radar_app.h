#ifndef __RADAR_APP_H__
#define __RADAR_APP_H__

#include <rtthread.h>

/* 初始化 LD2410C 人体存在雷达（PE14=OUT），成功返回 RT_EOK */
int radar_app_init(void);

/* 返回 1：有人存在，0：无人 */
int radar_is_presence(void);

#endif
