#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <onenet.h>
#include "onenet_app.h"

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet.app"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* ONENET_DEBUG */

#include <rtdbg.h>

#define PIN_LED_B              GET_PIN(F, 11)      // PF11 : 蓝灯（OneNET 远程控制）

#ifdef FINSH_USING_MSH
#include <finsh.h>

/* 周期性上传线程入口函数，每 5 秒向 OneNET 上报一个随机温度值 */
static void onenet_upload_entry(void *parameter)
{
    int value = 0;

    while (1)
    {
        value = rand() % 100;

        if (onenet_mqtt_upload_digit("temperature", value) < 0)
        {
            LOG_E("upload has an error, stop uploading");
            break;
        }
        else
        {
            LOG_D("buffer : {\"temperature\":%d}", value);
        }

        rt_thread_delay(rt_tick_from_millisecond(5 * 1000));
    }
}

/* 创建周期性上传线程并启动 */
int onenet_upload_cycle(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("onenet_send",
                           onenet_upload_entry,
                           RT_NULL,
                           2 * 1024,
                           RT_THREAD_PRIORITY_MAX / 3 - 1,
                           5);
    if (tid)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
MSH_CMD_EXPORT(onenet_upload_cycle, send data to OneNET cloud cycle);

/* MSH 命令：向 OneNET 上报数值型数据流 */
int onenet_publish_digit(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [value]  - mqtt pulish digit data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_digit(argv[1], atoi(argv[2])) < 0)
    {
        LOG_E("upload digit data has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_digit, onenet_mqtt_publish_digit, send digit data to onenet cloud);

/* MSH 命令：向 OneNET 上报字符串型数据流 */
int onenet_publish_string(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [string]  - mqtt pulish string data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_string(argv[1], argv[2]) < 0)
    {
        LOG_E("upload string has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_string, onenet_mqtt_publish_string, send string data to onenet cloud);

/*
 * OneNET 平台下发命令的响应回调函数
 * 控制蓝色 LED（PF11）：recv_data[1] > 0 点亮，否则熄灭。
 * 红色 LED（PF12）由燃气报警模块独占，不受远程控制。
 */
static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size)
{
    char res_buf[] = { "cmd is received!\n" };

    LOG_D("recv data is %x:%x\n", recv_data[0], recv_data[1]);

    if (recv_data[0] == 0x00)
    {
        rt_pin_write(PIN_LED_B, recv_data[1] > 0 ? PIN_HIGH : PIN_LOW);
        LOG_D("blue LED: %d", recv_data[1]);
    }

    *resp_data = (uint8_t *) ONENET_MALLOC(strlen(res_buf));
    strncpy((char *)*resp_data, res_buf, strlen(res_buf));
    *resp_size = strlen(res_buf);
}

/* MSH 命令：初始化蓝灯引脚并注册 OneNET 命令响应回调 */
int onenet_set_cmd_rsp(int argc, char **argv)
{
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
    return 0;
}
MSH_CMD_EXPORT(onenet_set_cmd_rsp, set cmd response function);

#endif /* FINSH_USING_MSH */
