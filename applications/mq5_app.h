#ifndef __MQ5_APP_H__
#define __MQ5_APP_H__

#include <rtthread.h>

/* 初始化 MQ-5 气体传感器 + 蜂鸣器 + 启动报警检测线程 */
int mq5_app_init(void);

/* 读取可燃气体报警状态：1 = 报警，0 = 正常（供显示/上报用） */
int mq5_app_is_alarm(void);

/* 查询蜂鸣器是否在响：1 = 响，0 = 静音 */
int buzzer_is_on(void);

/* 远程控制蜂鸣器开/关（燃气报警时自动覆盖为响） */
void buzzer_on(void);
void buzzer_off(void);

/* 查询是否被远程手动控制蜂鸣器 */
int buzzer_manual_mode(void);
void buzzer_set_manual(int manual);

/* 蜂鸣器短响指定毫数（不影响手动/自动状态） */
void buzzer_beep_ms(int ms);

#endif