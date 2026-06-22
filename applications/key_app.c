#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "key_app.h"

#define DBG_TAG "key"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define KEY_LEFT_PIN    GET_PIN(C, 0)   /* KEY_LEFT：板载最左边按键 */
#define KEY1_PIN        GET_PIN(C, 1)   /* KEY1：板载左数第二个按键 */
#define KEY2_PIN        GET_PIN(C, 4)   /* KEY2：PC4（避开 SPI2_MISO PC2） */

/*
 * 初始化所有按键引脚
 * 均配置为上拉输入模式，按下时读取为低电平。
 */
void key_app_init(void)
{
    rt_pin_mode(KEY_LEFT_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY1_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY2_PIN, PIN_MODE_INPUT_PULLUP);

    LOG_I("Keys initialized: KEY_LEFT(PC0), KEY1(PC1), KEY2(PC4)");
}

/*
 * 读取 KEY_LEFT（PC0）状态
 * 返回 1：按键按下
 * 返回 0：按键释放
 */
int key_left_is_pressed(void)
{
    return (rt_pin_read(KEY_LEFT_PIN) == PIN_LOW) ? 1 : 0;
}

/*
 * 读取 KEY1（PC1）状态
 * 返回 1：按键按下
 * 返回 0：按键释放
 */
int key1_is_pressed(void)
{
    return (rt_pin_read(KEY1_PIN) == PIN_LOW) ? 1 : 0;
}

/*
 * 读取 KEY2（PC2）状态
 * 返回 1：按键按下
 * 返回 0：按键释放
 */
int key2_is_pressed(void)
{
    return (rt_pin_read(KEY2_PIN) == PIN_LOW) ? 1 : 0;
}
