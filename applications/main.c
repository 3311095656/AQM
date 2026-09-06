/*
 *   Date            Author              Notes
 * 2026-4-25      JianZhenfeng       first version
 * 2026-5-03      -                  multi-thread refactoring
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "wifi_app.h"
#include "sensor_app.h"
#include "lcd_app.h"
#include "mqtt_app.h"
#include "fan_app.h"
#include "oled_app.h"
#include "mq5_app.h"
#include "servo_app.h"
#include "key_app.h"
#include "radar_app.h"
#include "ntp_app.h"
#include "ws2812b_app.h"
#include "ac_app.h"
#include "humidifier_app.h"
#include "schedule_app.h"
#include "voice_app.h"

#include <wlan_mgnt.h>
#include <sys/time.h>
#include <time.h>

#include <drivers/watchdog.h>

#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== 软件看门狗 ==================== */

/*
 * 软件看门狗实现
 * 原理：启动一个单次定时器（8秒），主循环每秒重新启动它。
 *       如果程序卡死，定时器到期触发 MCU 复位。
 */

#define SWDG_TIMEOUT_MS    8000   /* 超时 8 秒 */

static rt_timer_t swdg_timer = RT_NULL;

/* 定时器回调：超时即复位 */
static void swdg_timeout_cb(void *parameter)
{
    LOG_E("Software watchdog timeout! System reset...");
    rt_thread_mdelay(100);
    NVIC_SystemReset();  /* MCU 软件复位 */
}

/* 初始化并启动软件看门狗 */
static void swdg_init(void)
{
    swdg_timer = rt_timer_create("swdg",
                                  swdg_timeout_cb,
                                  RT_NULL,
                                  rt_tick_from_millisecond(SWDG_TIMEOUT_MS),
                                  RT_TIMER_FLAG_SOFT_TIMER | RT_TIMER_FLAG_ONE_SHOT);
    if (swdg_timer)
    {
        rt_timer_start(swdg_timer);
        LOG_I("Software watchdog started, timeout=%ds", SWDG_TIMEOUT_MS / 1000);
    }
    else
    {
        LOG_E("Software watchdog create failed!");
    }
}

/* 喂狗：主循环每秒调用一次，重启定时器 */
static void swdg_feed(void)
{
    if (swdg_timer)
    {
        rt_timer_stop(swdg_timer);
        rt_timer_start(swdg_timer);
    }
}

#define MQTT_PUB_INTERVAL  2

/* 传感器数据全局缓存（主线程写入，网络线程读取，互斥保护） */
static sensor_data_t g_sensor_data;
static struct rt_mutex g_data_mutex;
static int g_sensor_ok = 0;  /* 传感器是否初始化成功 */

/* ==================== 网络线程 ==================== */

/*
 * 网络线程入口
 * 按 KEY1 切换 WiFi+MQTT 连接/断开。
 * 已连接 → 按 KEY1 断开；已断开 → 按 KEY1 连接。
 */
static void network_thread_entry(void *parameter)
{
    int connected = 0;

    LOG_I("KEY1: WiFi/MQTT toggle | KEY2: manual/auto toggle");

    while (1)
    {
        /* 等待 KEY1 按下 */
        if (key1_is_pressed())
        {
            rt_thread_mdelay(50);
            if (key1_is_pressed())
            {
                if (connected)
                {
                    /* 已连接 → 断开 */
                    LOG_I("KEY1 pressed, disconnecting...");
                    rt_wlan_config_autoreconnect(RT_FALSE);  /* 关闭自动重连 */
                    rt_wlan_disconnect();                    /* 断开 WiFi */
                    connected = 0;
                }
                else
                {
                    /* 未连接 → 连接 */
                    LOG_I("KEY1 pressed, starting WiFi + MQTT...");
                    if (wifi_app_init() == RT_EOK)
                    {
                        mqtt_app_init();
                        connected = 1;
                        LOG_I("Network connected!");

                        /* 联网后自动同步 NTP 时间 */
                        ntp_app_sync();
                    }
                    else
                    {
                        LOG_E("WiFi init failed");
                    }
                }

                /* 等待按键释放，防止反复触发 */
                while (key1_is_pressed())
                    rt_thread_mdelay(50);
            }
        }

        /* KEY2 切换手动/自动模式（离线也可用） */
        if (key2_is_pressed())
        {
            rt_thread_mdelay(50);
            if (key2_is_pressed())
            {
                mqtt_app_toggle_auto_mode();

                while (key2_is_pressed())
                    rt_thread_mdelay(50);
            }
        }

        /* 已连接时周期性上报 */
        if (connected && mqtt_app_is_connected())
        {
            static int pub_count = 0;
            sensor_data_t data;

            rt_mutex_take(&g_data_mutex, RT_WAITING_FOREVER);
            data = g_sensor_data;
            rt_mutex_release(&g_data_mutex);

            pub_count++;
            if (pub_count >= MQTT_PUB_INTERVAL)
            {
                pub_count = 0;
                mqtt_app_publish_sensor(&data);
            }
        }

        swdg_feed();    /* 喂狗，防止软件看门狗复位 */
        rt_thread_mdelay(1000);
    }
}

/* ==================== 主函数 ==================== */

int main(void)
{
    /* ---- 初始化阶段 ---- */
    lcd_app_init();
    fan_app_init();

    /* 安全线程优先启动，传感器可按需重新初始化 */
    mq5_app_init();      /* 启动燃气报警线程（安全最高优先级） */

    if (sensor_app_init() == RT_EOK)
    {
        g_sensor_ok = 1;
        LOG_I("All sensors OK");
    }
    else
    {
        g_sensor_ok = 0;
        LOG_E("sensor init failed! Sensors disabled, safety threads still running.");
        /* 不退出：燃气报警等安全线程已启动，系统以安全模式运行 */
    }

    oled_app_init();
    servo_app_init();    /* 初始化 SG90 舵机 */
    radar_app_init();    /* 初始化 LD2410C 人体雷达 */
    key_app_init();
    ws2812b_app_init();  /* 初始化 WS2812B RGB LED */
    ac_app_init();       /* 初始化空调控制模块 */
    schedule_app_init(); /* 初始化定时计划模块 */
    humidifier_app_init(); /* 初始化加湿/除湿模块 */
    voice_app_init();      /* 初始化语音识别框架（硬件/模型未就绪时自动跳过） */
    swdg_init();         /* 启动软件看门狗（8 秒超时） */

    /* 初始化数据互斥锁 */
    rt_mutex_init(&g_data_mutex, "data_mtx", RT_IPC_FLAG_FIFO);

    /* 启动网络线程（等待 KEY1 触发 WiFi+MQTT） */
    rt_thread_t net_tid = rt_thread_create("network",
                                            network_thread_entry,
                                            RT_NULL,
                                            4096,
                                            12,
                                            5);
    if (net_tid)
        rt_thread_startup(net_tid);

    /* ---- 主循环：传感器 + 显示 + 风扇 ---- */
    while (1)
    {
        /* 读取传感器（仅当初始化成功）；失败时周期重试 */
        sensor_data_t data;
        rt_memset(&data, 0, sizeof(data));
        data.valid = 0;  /* 默认无效 */
        if (g_sensor_ok)
        {
            sensor_app_read(&data);
            data.valid = 1;  /* 读取成功，标记有效 */
        }
        else
        {
            static int retry_countdown = 0;
            if (--retry_countdown <= 0)
            {
                retry_countdown = 30;  /* 每 30 秒重试一次 */
                LOG_I("Retrying sensor init...");
                if (sensor_app_init() == RT_EOK)
                {
                    g_sensor_ok = 1;
                    LOG_I("Sensors recovered!");
                }
            }
        }

        /* 补充窗户状态和人体存在 */
        data.window_state = servo_window_is_open();
        data.presence     = radar_is_presence();

        /* 人体存在检测日志（紧接传感器数据） */
        LOG_D("indoor  P: %s", data.presence ? "Y" : "N");

        /* 写入全局缓存（网络线程使用） */
        rt_mutex_take(&g_data_mutex, RT_WAITING_FOREVER);
        g_sensor_data = data;
        rt_mutex_release(&g_data_mutex);

        /* 更新 LCD */
        lcd_app_update(&data);
        lcd_app_update_status(rt_wlan_is_connected(), mqtt_app_is_connected());
        lcd_app_update_mode(!(mqtt_app_fan_manual() || mqtt_app_window_manual() || ac_manual_mode() || humi_manual_mode()));
        lcd_app_update_datetime(rt_wlan_is_connected());

        /* 更新 OLED（室内数据 + 燃气状态 + 人体） */
        {
            oled_app_update(&data, !mq5_app_is_alarm(), data.presence);

            /* 人体状态变化时输出日志 */
            static int last_presence = -1;
            if (data.presence != last_presence)
            {
                last_presence = data.presence;
                LOG_I("Radar: %s", data.presence ? "YES" : "NO");
            }
        }

        /* 舵机 & 风扇控制：燃气报警强制动作（安全优先），正常时尊重手动模式 */
        {
            static int last_alarm = 0;
            int alarm = mq5_app_is_alarm();

            if (alarm)
            {
                if (!last_alarm)
                    LOG_I("Servo: Window OPEN (gas alarm)");
                servo_window_open();
            }
            else if (!mqtt_app_window_manual() && g_sensor_ok)
            {
                if (last_alarm)
                    LOG_I("Servo: Window CLOSE (gas safe)");
                /* 自动模式 + 有人：根据光照强度调节窗开度（0 lux→45°, 1000 lux→0°, 每200lux减9°） */
                if (radar_is_presence())
                {
                    int lux = (int)data.brightness;
                    int angle;
                    if (lux >= 1000)
                        angle = 0;
                    else if (lux >= 800)
                        angle = 9;
                    else if (lux >= 600)
                        angle = 18;
                    else if (lux >= 400)
                        angle = 27;
                    else if (lux >= 200)
                        angle = 36;
                    else
                        angle = 45;
                    servo_window_set_angle(angle);
                }
                else
                {
                    servo_window_close();
                }
            }

            if (alarm)
            {
                if (!last_alarm)
                    LOG_I("Fan: ON (gas alarm)");
                fan_on();
            }
            else if (!mqtt_app_fan_manual())
            {
                if (last_alarm)
                    LOG_I("Fan: OFF (gas safe)");
                fan_off();
            }

            last_alarm = alarm;
        }

        /* 空调 & 加湿/除湿控制：燃气报警强制关闭，正常时自动/手动控制 */
        {
            int alarm = mq5_app_is_alarm();

            /* 定时计划检查：根据 NTP 时间执行定时任务 */
            {
                static int last_sch = -2;
                static int saved_ac_manual = 0, saved_humi_manual = 0;
                static int saved_ac_mode = AC_OFF, saved_humi_mode = HUMI_OFF;
                static int saved_auto_state = 0;  /* 进入计划前的自动/手动状态 */

                time_t now_sec = time(RT_NULL);
                struct tm *now = localtime(&now_sec);
                int sch = schedule_check(now->tm_hour, now->tm_min);

                /* 进入计划：保存完整原始状态 */
                if (sch >= 0 && last_sch < 0)
                {
                    saved_ac_manual = ac_manual_mode();
                    saved_humi_manual = humi_manual_mode();
                    saved_ac_mode = (int)ac_get_mode();
                    saved_humi_mode = (int)humi_get_mode();
                    saved_auto_state = mqtt_app_auto_mode();  /* 保存全体自动/手动状态 */
                }

                if (sch >= 0)
                {
                    /* 在时间段内，根据模式执行 */
                    int mode = schedule_get_mode(sch);
                    int action = schedule_get_action(sch);
                    if (mode == SCHED_MODE_TEMP)
                    {
                        ac_set_manual(1);   /* 锁住，不让自动温控覆盖 */
                        ac_set_mode(action == 0 ? AC_COOL : AC_HEAT);
                        LOG_I("Schedule: AC %s (force)", action == 0 ? "COOL" : "HEAT");
                    }
                    else if (mode == SCHED_MODE_HUMI)
                    {
                        humi_set_manual(1); /* 锁住，不让自动湿度控制覆盖 */
                        humi_set_mode(action == 0 ? HUMI_HUMIDIFY : HUMI_DEHUMIDIFY);
                        LOG_I("Schedule: HUMI %s (force)", action == 0 ? "HUMIDIFY" : "DEHUMIDIFY");
                    }
                    else /* SCHED_MODE_SYSTEM */
                    {
                        if (!mqtt_app_auto_mode())
                        {
                            mqtt_set_auto_mode_remote(1);
                            LOG_I("Schedule: AUTO (system)");
                        }
                    }
                }
                else if ((sch == -2 || sch == -1) && last_sch >= 0)
                {
                    /* 离开定时时间段或计划被禁用，恢复进入前的完整原始状态 */
                    ac_set_manual(saved_ac_manual);
                    humi_set_manual(saved_humi_manual);
                    ac_set_mode((ac_mode_t)saved_ac_mode);
                    humi_set_mode((humi_mode_t)saved_humi_mode);
                    /* 恢复风扇/窗户/蜂鸣器自动/手动状态 */
                    mqtt_set_auto_mode_remote(saved_auto_state ? 1 : 0);
                    LOG_I("Schedule: left period, restored AC=%d/%d Humi=%d/%d auto=%d",
                          saved_ac_mode, saved_ac_manual, saved_humi_mode, saved_humi_manual,
                          saved_auto_state);
                }

                last_sch = sch;
            }

            /* 燃气报警时强制关闭所有环境设备（开窗+风扇优先） */
            if (alarm)
            {
                ac_force_off();
                humi_force_off();
            }
            else if (g_sensor_ok)  /* 传感器正常才执行自动控制 */
            {
                /* 空调自动控制（自动模式下，根据室内温度调节） */
                if (!ac_manual_mode())
                {
                    if (ac_auto_control(data.in_temperature, ac_get_target_temp()))
                    {
                        const char *mode_str[] = {"OFF", "COOL", "HEAT", "AUTO"};
                        LOG_I("AC: %s (in=%.1f, target=%.1f)", mode_str[ac_get_mode()], data.in_temperature, ac_get_target_temp());
                    }
                }

                /* 加湿/除湿自动控制（自动模式下，根据室内湿度调节） */
                if (!humi_manual_mode())
                {
                    if (humi_auto_control(data.in_humidity, humi_get_target_humi()))
                    {
                        const char *mode_str[] = {"OFF", "HUMIDIFY", "DEHUMIDIFY", "AUTO"};
                        LOG_I("Humi: %s (in=%.1f, target=%.1f)", mode_str[humi_get_mode()], data.in_humidity, humi_get_target_humi());
                    }
                }
            }
        }

        swdg_feed();    /* 喂看门狗 */
        rt_thread_mdelay(1000);
    }

    return 0;
}
