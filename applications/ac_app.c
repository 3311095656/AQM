#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "ac_app.h"
#include "ws2812b_app.h"

#define DBG_TAG "ac"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== 空调控制模块 ==================== */

/*
 * 空调模拟控制
 * 使用 WS2812B LED 颜色表示空调状态：
 *   蓝色 = 制冷，红色 = 制热，灭 = 关闭
 * （仅用 LED 指示，不接继电器）
 */

/* 温度回滞：目标温度 ±1°C 内不动作，防止频繁启停 */
#define TEMP_HYSTERESIS 0.5f

/* 目标温度范围 */
#define TARGET_TEMP_MIN     16.0f
#define TARGET_TEMP_MAX     40.0f

static ac_mode_t current_mode = AC_OFF;     /* 当前空调模式 */
static int manual_flag = 1;                  /* 手动/自动标志：1=手动，0=自动 */
static int ac_running = 0;                   /* 当前是否在运行 */
static float target_temp = 26.0f;           /* 目标温度，初始 26°C */

/*
 * 更新 WS2812B LED 颜色，反映空调状态
 */
static void update_led(void)
{
    switch (current_mode)
    {
    case AC_COOL:
        ws2812b_set_color(WS2812B_LED_AC, COLOR_BLUE);   /* 蓝色 = 制冷 */
        ac_running = 1;
        break;
    case AC_HEAT:
        ws2812b_set_color(WS2812B_LED_AC, COLOR_RED);    /* 红色 = 制热 */
        ac_running = 1;
        break;
    case AC_AUTO:
        ws2812b_set_color(WS2812B_LED_AC, COLOR_GREEN);  /* 绿色 = 自动 */
        ac_running = 1;
        break;
    default:
        ws2812b_set_off(WS2812B_LED_AC);                 /* 灭 = 关闭 */
        ac_running = 0;
        break;
    }
}

/*
 * 初始化空调模块
 * 初始化 WS2812B LED 为关闭状态
 */
int ac_app_init(void)
{
    current_mode = AC_OFF;
    ac_running = 0;
    manual_flag = 0;

    update_led();

    LOG_I("AC module init OK (WS2812B LED only)");
    return RT_EOK;
}

/* 设置空调模式 */
void ac_set_mode(ac_mode_t mode)
{
    current_mode = mode;
    update_led();
}

/* 获取当前空调模式 */
ac_mode_t ac_get_mode(void)
{
    return current_mode;
}

/* 空调是否在运行 */
int ac_is_running(void)
{
    return ac_running;
}

/* 手动/自动模式 */
int ac_manual_mode(void)
{
    return manual_flag;
}

void ac_set_manual(int manual)
{
    manual_flag = manual;
}

/* 强制关闭（燃气报警时调用） */
void ac_force_off(void)
{
    if (current_mode != AC_OFF)
    {
        current_mode = AC_OFF;
        update_led();
        LOG_I("AC: Force OFF (gas alarm)");
    }
}

/*
 * 自动温控
 * 使用内部 target_temp，回滞 ±0.5°C
 *   current > target + 0.5  → 制冷（蓝灯）
 *   current < target - 0.5  → 制热（红灯）
 *   其他                    → AC_AUTO（绿灯，待机）
 * 返回 1 表示状态变化
 */
int ac_auto_control(float current_temp, float target)
{
    ac_mode_t new_mode;

    if (current_temp > target + TEMP_HYSTERESIS)
        new_mode = AC_COOL;
    else if (current_temp < target - TEMP_HYSTERESIS)
        new_mode = AC_HEAT;
    else
        new_mode = AC_AUTO;  /* 温度正常时保持自动模式，LED 亮绿灯 */

    if (new_mode != current_mode)
    {
        ac_set_mode(new_mode);
        return 1;
    }
    return 0;
}

/* 获取当前目标温度 */
float ac_get_target_temp(void)
{
    return target_temp;
}

/*
 * 设置目标温度
 * 范围 16~30°C，步长 0.5°C
 * 返回实际设置后的值
 */
float ac_set_target_temp(float temp)
{
    /* 四舍五入到 0.5 的倍数 */
    temp = (float)((int)(temp * 2 + 0.5f)) / 2.0f;

    if (temp < TARGET_TEMP_MIN) temp = TARGET_TEMP_MIN;
    if (temp > TARGET_TEMP_MAX) temp = TARGET_TEMP_MAX;

    target_temp = temp;
    LOG_I("Target temp: %d.%d C", (int)target_temp, 
         (int)((target_temp - (int)target_temp) * 10 + 0.5f) % 10);
    return target_temp;
}
