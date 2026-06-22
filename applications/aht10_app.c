#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "aht10.h"
#include "aht10_app.h"

#define DBG_TAG "aht10"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

static aht10_device_t dev_aht10 = RT_NULL;

/*
 * 初始化 AHT10 室外温湿度传感器
 * 挂载在 I2C3 总线上。
 * 成功返回 RT_EOK，失败返回 -RT_ERROR。
 */
int aht10_app_init(void)
{
    dev_aht10 = aht10_init("i2c3");
    if (dev_aht10 == RT_NULL)
    {
        LOG_E("aht10 init failed on i2c3");
        return -RT_ERROR;
    }

    LOG_I("AHT10 (outdoor) initialized on i2c3");
    return RT_EOK;
}

/*
 * 读取室外温度（AHT10）
 * 返回温度值，单位：摄氏度（℃）
 */
float aht10_app_read_temperature(void)
{
    if (dev_aht10 == RT_NULL)
        return 0.0f;
    return aht10_read_temperature(dev_aht10);
}

/*
 * 读取室外湿度（AHT10）
 * 返回湿度值，单位：百分比（%）
 */
float aht10_app_read_humidity(void)
{
    if (dev_aht10 == RT_NULL)
        return 0.0f;
    return aht10_read_humidity(dev_aht10);
}
