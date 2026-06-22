#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "servo_app.h"

#define DBG_TAG "servo"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define SERVO_PIN           GET_PIN(E, 13)  /* 窗舵机：PE13（40Pin → Pin 23） */

#define SERVO_PERIOD_US     20000           /* 20ms 周期（50Hz） */
#define SERVO_MIN_US        500             /* 0° 对应 0.5ms */
#define SERVO_MAX_US        1167            /* 60° 对应 1.17ms */

#define WINDOW_CLOSE_US     500             /* 关窗脉宽（0°） */
#define WINDOW_OPEN_US      1000            /* 开窗脉宽（45°） */

extern void rt_hw_us_delay(rt_uint32_t us);

static volatile int window_pulse_us = WINDOW_CLOSE_US;
static volatile int window_angle = 0;                  /* 当前角度 0~45° */

/*
 * 角度 → 脉宽微秒 线性映射
 */
static int angle_to_us(int degree)
{
    if (degree <= 0)  return WINDOW_CLOSE_US;
    if (degree >= 45) return WINDOW_OPEN_US;
    /* 0°=500μs, 45°=1000μs → 每度 (500/45)≈11.11μs */
    return WINDOW_CLOSE_US + (degree * 500) / 45;
}

/*
 * 舵机驱动线程
 * 每 20ms 输出一路 PWM。
 * 脉冲输出期间关中断，防止中断延迟拉长脉宽导致舵机抖动。
 * 剩余时间用 rt_thread_mdelay 释放 CPU 给其他线程。
 */
static void servo_thread_entry(void *parameter)
{
    while (1)
    {
        int pulse = window_pulse_us;

        if (pulse < SERVO_MIN_US) pulse = SERVO_MIN_US;
        if (pulse > SERVO_MAX_US) pulse = SERVO_MAX_US;

        rt_enter_critical();
        rt_pin_write(SERVO_PIN, PIN_HIGH);
        rt_hw_us_delay(pulse);
        rt_pin_write(SERVO_PIN, PIN_LOW);
        rt_exit_critical();

        rt_thread_mdelay(20);
    }
}

/*
 * 初始化窗舵机
 */
int servo_app_init(void)
{
    rt_pin_mode(SERVO_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SERVO_PIN, PIN_LOW);

    rt_thread_t tid = rt_thread_create("servo",
                                        servo_thread_entry,
                                        RT_NULL,
                                        512,
                                        10,   /* 普通优先级，不抢占关键线程 */
                                        5);
    if (tid)
        rt_thread_startup(tid);

    LOG_I("Window servo on PE13");
    return RT_EOK;
}

void servo_window_open(void)
{
    window_pulse_us = WINDOW_OPEN_US;
    window_angle = 45;
}

void servo_window_close(void)
{
    window_pulse_us = WINDOW_CLOSE_US;
    window_angle = 0;
}

void servo_window_set_angle(int degree)
{
    if (degree < 0)  degree = 0;
    if (degree > 45) degree = 45;
    window_angle = degree;
    window_pulse_us = angle_to_us(degree);
}

int servo_window_get_angle(void)
{
    return window_angle;
}

int servo_window_is_open(void)
{
    return (window_angle > 0) ? 1 : 0;
}
