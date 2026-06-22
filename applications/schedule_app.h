#ifndef APPLICATIONS_SCHEDULE_APP_H_
#define APPLICATIONS_SCHEDULE_APP_H_

#include <rtthread.h>
#include "cJSON.h"

/* ==================== 定时计划模块 ==================== */

/* 最多支持 4 个时间段 */
#define SCHEDULE_MAX_PERIODS    4

/* 定时模式类型 */
typedef enum {
    SCHED_MODE_SYSTEM = 0,   /* 系统自动模式 */
    SCHED_MODE_TEMP,         /* 温度控制 */
    SCHED_MODE_HUMI          /* 湿度控制 */
} schedule_mode_t;

/* 动作类型（温度控制用） */
typedef enum {
    SCHED_ACT_COOL = 0,      /* 制冷 */
    SCHED_ACT_HEAT           /* 制热 */
} schedule_act_temp_t;

/* 动作类型（湿度控制用） */
typedef enum {
    SCHED_ACT_HUMIDIFY = 0,  /* 加湿 */
    SCHED_ACT_DEHUMIDIFY     /* 除湿 */
} schedule_act_humi_t;

/* 单个时间段结构 */
typedef struct {
    int start_hour;     /* 开始时 (0~23) */
    int start_min;      /* 开始分 (0~59) */
    int end_hour;       /* 结束时 (0~23) */
    int end_min;        /* 结束分 (0~59) */
    int enabled;        /* 1=此时间段启用，0=禁用 */
    int mode;           /* 0=系统自动, 1=温度控制, 2=湿度控制 */
    int action;         /* temp时: 0=制冷/1=制热; humi时: 0=加湿/1=除湿 */
} schedule_period_t;

/* 初始化定时计划模块 */
void schedule_app_init(void);

/* 设置定时计划配置（来自云端下发） */
void schedule_set_config(int enable, cJSON *periods_json);

/* 检查当前是否在某个计划时间段内
 * 返回 >=0 = 匹配到的时段索引，-1=定时未启用，-2=不在任何时段内
 */
int schedule_check(int hour, int min);

/* 获取匹配时段的模式和动作（由 main.c 调用） */
int schedule_get_mode(int index);
int schedule_get_action(int index);

/* 获取定时计划启用状态 */
int schedule_is_enabled(void);

/* 获取时间段数量 */
int schedule_get_period_count(void);

/* 获取指定时间段 */
schedule_period_t schedule_get_period(int index);

#endif /* APPLICATIONS_SCHEDULE_APP_H_ */
