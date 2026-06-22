#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "radar_app.h"

#define DBG_TAG "radar"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define RADAR_OUT_PIN   GET_PIN(E, 14)  /* LD2410C OUT 引脚：PE14（40Pin → Pin 25） */

/*
 * 初始化 LD2410C 雷达
 * OUT 引脚配置为上拉输入，高电平=有人，低电平=无人。
 */
int radar_app_init(void)
{
    rt_pin_mode(RADAR_OUT_PIN, PIN_MODE_INPUT_PULLDOWN);
    LOG_I("LD2410C radar initialized on PE14");
    return RT_EOK;
}

/*
 * 读取人体存在状态
 * 返回 1：有人存在
 * 返回 0：无人
 */
int radar_is_presence(void)
{
    return (rt_pin_read(RADAR_OUT_PIN) == PIN_HIGH) ? 1 : 0;
}
