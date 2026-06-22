#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mq5_app.h"
#include "key_app.h"

#define DBG_TAG "mq5"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define GAS_DO_PIN      GET_PIN(A, 0)   /* MQ-5 DO 引脚 */
#define BEEP_PIN        GET_PIN(B, 0)   /* 蜂鸣器 */
#define LED_RED_PIN     GET_PIN(F, 12)  /* 红灯：燃气报警 */

/* 报警状态（供其他模块读取） */
static volatile int alarm_state = 0;

/* 蜂鸣器远程手动控制 */
static volatile int buzzer_manual = 1;   /* 1=手动模式 */
static volatile int buzzer_manual_on = 0; /* 手动模式下蜂鸣器状态 */

/*
 * 燃气报警检测线程（200ms 周期）
 * 检查 MQ-5 + KEY_LEFT，控制蜂鸣器和红灯。
 * 优先级设为 8，高于主显示线程，确保报警及时响应。
 */
static void mq5_alarm_thread_entry(void *parameter)
{
    static int last_state = -1;

    while (1)
    {
        int alarming = 0;

        /* MQ-5 传感器触发（DO 低电平 = 检测到可燃气体） */
        if (rt_pin_read(GAS_DO_PIN) == PIN_LOW)
            alarming = 1;

        /* KEY_LEFT 模拟报警 */
        if (key_left_is_pressed())
            alarming = 1;

        alarm_state = alarming;

        /* 状态变化时输出日志 */
        if (alarming != last_state)
        {
            last_state = alarming;
            if (alarming)
                LOG_I("Gas: WARNING! Buzzer ON, Red LED ON");
            else
                LOG_I("Gas: SAFE, Buzzer OFF, Red LED OFF");
        }

        /* 控制蜂鸣器：燃气报警强制响，正常时看手动/自动 */
        if (alarming)
        {
            rt_pin_write(BEEP_PIN, PIN_HIGH);
        }
        else if (buzzer_manual)
        {
            rt_pin_write(BEEP_PIN, buzzer_manual_on ? PIN_HIGH : PIN_LOW);
        }
        else
        {
            rt_pin_write(BEEP_PIN, PIN_LOW);
        }

        /* 红灯跟随报警状态 */
        rt_pin_write(LED_RED_PIN, alarming ? PIN_LOW : PIN_HIGH);

        rt_thread_mdelay(200);
    }
}

int mq5_app_init(void)
{
    /* MQ-5 DO 引脚：上拉输入 */
    rt_pin_mode(GAS_DO_PIN, PIN_MODE_INPUT_PULLUP);

    /* 蜂鸣器：推挽输出，默认关闭 */
    rt_pin_mode(BEEP_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(BEEP_PIN, PIN_LOW);

    /* 红灯：推挽输出，默认关闭（高电平灭） */
    rt_pin_mode(LED_RED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_RED_PIN, PIN_HIGH);

    LOG_I("MQ-5 on PA0, Buzzer on PB0, Red LED on PF12");

    /* 创建报警检测线程 */
    rt_thread_t tid = rt_thread_create("mq5_alarm",
                                        mq5_alarm_thread_entry,
                                        RT_NULL,
                                        1024,
                                        8,   /* 高优先级，及时报警 */
                                        5);
    if (tid)
        rt_thread_startup(tid);

    return RT_EOK;
}

int mq5_app_is_alarm(void)
{
    return alarm_state;
}

/*
 * 查询蜂鸣器当前状态
 * 读取 BEEP_PIN 引脚电平，HIGH = 响，LOW = 静音。
 */
int buzzer_is_on(void)
{
    return rt_pin_read(BEEP_PIN) == PIN_HIGH;
}

/*
 * 远程打开蜂鸣器（进入手动模式）
 * 燃气报警线程检测到手动标志后会保持蜂鸣器响。
 */
void buzzer_on(void)
{
    buzzer_manual = 1;
    buzzer_manual_on = 1;
}

/*
 * 远程关闭蜂鸣器（进入手动模式）
 * 燃气报警线程检测到手动标志后会保持蜂鸣器静音。
 * 注意：燃气报警时会强制覆盖为响，不受手动控制。
 */
void buzzer_off(void)
{
    buzzer_manual = 1;
    buzzer_manual_on = 0;
}

/*
 * 查询蜂鸣器是否处于远程手动模式
 * 返回 1 = 手动模式，0 = 自动模式
 */
int buzzer_manual_mode(void)
{
    return buzzer_manual;
}

/*
 * 设置蜂鸣器手动/自动模式
 * manual=1 进入手动，manual=0 恢复自动（清零手动状态）
 */
void buzzer_set_manual(int manual)
{
    buzzer_manual = manual;
    if (!manual)
        buzzer_manual_on = 0;
}

/*
 * 蜂鸣器短响指定毫数（不影响手动/自动状态）
 * 用于收到远程命令时的提示音
 */
void buzzer_beep_ms(int ms)
{
    rt_pin_write(BEEP_PIN, PIN_HIGH);
    rt_thread_mdelay(ms);
    rt_pin_write(BEEP_PIN, PIN_LOW);
}
