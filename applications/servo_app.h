#ifndef __SERVO_APP_H__
#define __SERVO_APP_H__

#include <rtthread.h>

/* 初始化窗舵机（PE15），成功返回 RT_EOK */
int servo_app_init(void);

/* 开窗（45°） */
void servo_window_open(void);

/* 关窗（0°） */
void servo_window_close(void);

/* 设置窗户角度（0~45°） */
void servo_window_set_angle(int degree);

/* 获取当前窗户角度 */
int servo_window_get_angle(void);

/* 查询窗户状态：1=开，0=关 */
int servo_window_is_open(void);

#endif
