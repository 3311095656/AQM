#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "schedule_app.h"
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#define DBG_TAG "schedule"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== 定时计划模块 ==================== */

static int schedule_enabled = 0;                            /* 定时计划开关 */
static schedule_period_t periods[SCHEDULE_MAX_PERIODS];     /* 时间段数组 */
static int period_count = 0;                                /* 当前时间段数量 */

/*
 * 初始化定时计划模块
 * 默认禁用，等待云端下发配置
 */
void schedule_app_init(void)
{
    schedule_enabled = 0;
    period_count = 0;
    rt_memset(periods, 0, sizeof(periods));
    LOG_I("Schedule module init OK");
}

/*
 * 设置定时计划配置
 * enable: 1=启用，0=禁用
 * periods_json: cJSON 数组，每个元素：
 *   {"start":"HH:MM","end":"HH:MM","enabled":true,"mode":"temp"/"humi"/"system","action":"cool"/"heat"/"humidify"/"dehumidify"}
 */
void schedule_set_config(int enable, cJSON *periods_json)
{
    schedule_enabled = enable;
    period_count = 0;

    if (periods_json && cJSON_IsArray(periods_json))
    {
        int count = cJSON_GetArraySize(periods_json);
        if (count > SCHEDULE_MAX_PERIODS)
            count = SCHEDULE_MAX_PERIODS;

        int i;
        for (i = 0; i < count; i++)
        {
            cJSON *item = cJSON_GetArrayItem(periods_json, i);
            cJSON *start = cJSON_GetObjectItem(item, "start");
            cJSON *end   = cJSON_GetObjectItem(item, "end");
            if (start && start->valuestring && end && end->valuestring)
            {
                int sh = 0, sm = 0, eh = 0, em = 0;
                if (sscanf(start->valuestring, "%d:%d", &sh, &sm) == 2 &&
                    sscanf(end->valuestring,   "%d:%d", &eh, &em) == 2)
                {
                    periods[period_count].start_hour = sh;
                    periods[period_count].start_min  = sm;
                    periods[period_count].end_hour   = eh;
                    periods[period_count].end_min    = em;

                    /* 解析新增字段 */
                    cJSON *ena = cJSON_GetObjectItem(item, "enabled");
                    periods[period_count].enabled = (ena && cJSON_IsTrue(ena)) ? 1 : 0;

                    cJSON *mod = cJSON_GetObjectItem(item, "mode");
                    periods[period_count].mode = SCHED_MODE_SYSTEM;  /* 默认系统自动 */
                    if (mod && mod->valuestring)
                    {
                        if (rt_strcmp(mod->valuestring, "temp") == 0)
                            periods[period_count].mode = SCHED_MODE_TEMP;
                        else if (rt_strcmp(mod->valuestring, "humi") == 0)
                            periods[period_count].mode = SCHED_MODE_HUMI;
                    }

                    cJSON *act = cJSON_GetObjectItem(item, "action");
                    periods[period_count].action = 0;  /* 默认制冷/加湿 */
                    if (act && act->valuestring)
                    {
                        if (rt_strcmp(act->valuestring, "heat") == 0 ||
                            rt_strcmp(act->valuestring, "dehumidify") == 0)
                            periods[period_count].action = 1;
                    }

                    period_count++;
                    const char *mode_str[] = {"SYS", "TEMP", "HUMI"};
                    const char *act_str[] = {"cool/humi", "heat/dehumi"};
                    LOG_I("Period %d: %02d:%02d-%02d:%02d en=%d %s %s",
                          period_count, sh, sm, eh, em,
                          periods[period_count-1].enabled,
                          mode_str[periods[period_count-1].mode],
                          act_str[periods[period_count-1].action]);
                }
            }
        }
    }

    LOG_I("Schedule config: enable=%d, periods=%d", schedule_enabled, period_count);
}

/*
 * 检查当前时间是否在某个计划时间段内
 * hour: 当前小时 (0~23)
 * min:  当前分钟 (0~59)
 * 返回: >=0 = 匹配到的时段索引，-1=定时未启用，-2=不在任何时段内
 */
int schedule_check(int hour, int min)
{
    if (!schedule_enabled)
        return -1;

    int current = hour * 60 + min;  /* 转为分钟便于比较 */
    int i;

    for (i = 0; i < period_count; i++)
    {
        /* 跳过被禁用的时段 */
        if (!periods[i].enabled)
            continue;

        int start = periods[i].start_hour * 60 + periods[i].start_min;
        int end   = periods[i].end_hour   * 60 + periods[i].end_min;

        /* 处理跨午夜（如 22:00 - 06:00） */
        if (start <= end)
        {
            if (current >= start && current < end)
                return i;
        }
        else
        {
            if (current >= start || current < end)
                return i;
        }
    }

    return -2;
}

/* 获取匹配时段的模式 */
int schedule_get_mode(int index)
{
    if (index >= 0 && index < period_count)
        return periods[index].mode;
    return SCHED_MODE_SYSTEM;
}

/* 获取匹配时段的动作 */
int schedule_get_action(int index)
{
    if (index >= 0 && index < period_count)
        return periods[index].action;
    return 0;
}

/* 获取定时计划启用状态 */
int schedule_is_enabled(void)
{
    return schedule_enabled;
}

/* 获取时间段数量 */
int schedule_get_period_count(void)
{
    return period_count;
}

/* 获取指定时间段 */
schedule_period_t schedule_get_period(int index)
{
    schedule_period_t empty = {0, 0, 0, 0};
    if (index >= 0 && index < period_count)
        return periods[index];
    return empty;
}
