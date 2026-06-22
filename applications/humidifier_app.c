#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "humidifier_app.h"
#include "ws2812b_app.h"

#define DBG_TAG "humi"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== 加湿/除湿控制模块 ==================== */

/*
 * 加湿/除湿模拟控制
 * 使用 WS2812B LED 颜色表示状态（第1颗）：
 *   紫色 = 加湿，青色 = 除湿，绿色 = 自动，灭 = 关闭
 */

/* 湿度回滞：目标湿度 ±3% 内不动作，防止频繁启停 */
#define HUMI_HYSTERESIS 3.0f

/* 目标湿度范围 */
#define TARGET_HUMI_MIN     20.0f
#define TARGET_HUMI_MAX     90.0f

static humi_mode_t current_mode = HUMI_OFF;
static int manual_flag = 1;
static int humi_running = 0;
static float target_humi = 55.0f;          /* 目标湿度，初始 55% */

/*
 * 更新 WS2812B LED 颜色，反映加湿/除湿状态
 */
static void update_led(void)
{
    switch (current_mode)
    {
    case HUMI_HUMIDIFY:
        ws2812b_set_color(WS2812B_LED_HUMI, COLOR_MAGENTA);  /* 紫色 = 加湿 */
        humi_running = 1;
        break;
    case HUMI_DEHUMIDIFY:
        ws2812b_set_color(WS2812B_LED_HUMI, COLOR_CYAN);     /* 青色 = 除湿 */
        humi_running = 1;
        break;
    case HUMI_AUTO:
        ws2812b_set_color(WS2812B_LED_HUMI, COLOR_GREEN);    /* 绿色 = 自动 */
        humi_running = 1;
        break;
    default:
        ws2812b_set_off(WS2812B_LED_HUMI);                   /* 灭 = 关闭 */
        humi_running = 0;
        break;
    }
}

/*
 * 初始化加湿/除湿模块
 */
int humidifier_app_init(void)
{
    current_mode = HUMI_OFF;
    humi_running = 0;
    manual_flag = 1;

    update_led();

    LOG_I("Humidifier module init OK (WS2812B LED)");
    return RT_EOK;
}

/* 设置模式 */
void humi_set_mode(humi_mode_t mode)
{
    current_mode = mode;
    update_led();
}

/* 获取当前模式 */
humi_mode_t humi_get_mode(void)
{
    return current_mode;
}

/* 是否在运行 */
int humi_is_running(void)
{
    return humi_running;
}

/* 手动/自动模式 */
int humi_manual_mode(void)
{
    return manual_flag;
}

void humi_set_manual(int manual)
{
    manual_flag = manual;
}

/* 强制关闭（燃气报警时调用） */
void humi_force_off(void)
{
    if (current_mode != HUMI_OFF)
    {
        current_mode = HUMI_OFF;
        update_led();
        LOG_I("Humi: Force OFF (gas alarm)");
    }
}

/*
 * 自动湿度控制
 *   current > target + 3  → 除湿（青色）
 *   current < target - 3  → 加湿（紫色）
 *   其他                  → HUMI_AUTO（绿色，待机）
 * 返回 1 表示状态变化
 */
int humi_auto_control(float current_humi, float target_humi)
{
    humi_mode_t new_mode;

    if (current_humi > target_humi + HUMI_HYSTERESIS)
        new_mode = HUMI_DEHUMIDIFY;
    else if (current_humi < target_humi - HUMI_HYSTERESIS)
        new_mode = HUMI_HUMIDIFY;
    else
        new_mode = HUMI_AUTO;  /* 湿度正常时保持自动模式，LED 亮绿灯 */

    if (new_mode != current_mode)
    {
        humi_set_mode(new_mode);
        return 1;
    }
    return 0;
}

/* 获取当前目标湿度 */
float humi_get_target_humi(void)
{
    return target_humi;
}

/*
 * 设置目标湿度
 * 范围 20~90%，步长 5%
 * 返回实际设置后的值
 */
float humi_set_target_humi(float humi)
{
    /* 四舍五入到 5 的倍数 */
    humi = (float)((int)(humi / 5.0f + 0.5f)) * 5.0f;

    if (humi < TARGET_HUMI_MIN) humi = TARGET_HUMI_MIN;
    if (humi > TARGET_HUMI_MAX) humi = TARGET_HUMI_MAX;

    target_humi = humi;
    LOG_I("Target humi: %d%%", (int)target_humi);
    return target_humi;
}
