#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "aht10_app.h"
#include "ap3216c_app.h"
#include "dht22_app.h"
#include "sensor_app.h"

#define DBG_TAG "sensor"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== 中值滤波 ==================== */

/* 比较函数，供 qsort 使用 */
static int float_cmp(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/*
 * 中值滤波：对某个传感器连续采样 3 次，取中间值
 * func: 读取函数，返回 float
 * 返回: 3 次采样的中值
 */
typedef float (*read_func_t)(void);
static float median_filter(read_func_t func)
{
    float buf[3];
    buf[0] = func();
    buf[1] = func();
    buf[2] = func();
    qsort(buf, 3, sizeof(float), float_cmp);
    return buf[1];  /* 中间值 */
}

/*
 * 初始化所有传感器模块
 * AHT10（I2C3）室外温湿度
 * AP3216C（I2C2）光照强度
 * DHT22（PA8）室内温湿度
 * 成功返回 RT_EOK，任一失败返回 -RT_ERROR。
 */
int sensor_app_init(void)
{
    if (aht10_app_init() != RT_EOK)
    {
        LOG_E("AHT10 init failed");
        return -RT_ERROR;
    }

    if (ap3216c_app_init() != RT_EOK)
    {
        LOG_E("AP3216C init failed");
        return -RT_ERROR;
    }

    if (dht22_app_init() != RT_EOK)
    {
        LOG_E("DHT22 init failed");
        return -RT_ERROR;
    }

    return RT_EOK;
}

/*
 * 读取所有传感器数据
 * 室外：AHT10 温度/湿度 + AP3216C 光照
 * 室内：DHT22 温度/湿度
 */
void sensor_app_read(sensor_data_t *data)
{
    if (data == RT_NULL)
        return;

    /* 室外数据：AHT10（中值滤波，3 次采样取中间值，过滤偶发异常） */
    data->out_temperature = median_filter(aht10_app_read_temperature);
    data->out_humidity    = median_filter(aht10_app_read_humidity);

    /* 光照数据：AP3216C（中值滤波） */
    data->brightness = median_filter(ap3216c_app_read_light);

    /* 室内数据：DHT22 */
    if (!dht22_app_read(&data->in_temperature, &data->in_humidity))
    {
        LOG_D("DHT22 read failed, using last value");
    }

    LOG_D("outdoor T: %d.%d  H: %d.%d%%  L: %d.%d",
            (int)data->out_temperature, (int)(data->out_temperature * 10) % 10,
            (int)data->out_humidity, (int)(data->out_humidity * 10) % 10,
            (int)data->brightness, ((int)(10 * data->brightness) % 10));
    LOG_D("indoor  T: %d.%d  H: %d.%d%%",
            (int)data->in_temperature, (int)(data->in_temperature * 10) % 10,
            (int)data->in_humidity, (int)(data->in_humidity * 10) % 10);
}
