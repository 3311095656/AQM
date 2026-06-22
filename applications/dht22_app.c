#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "dhtxx.h"
#include "dht22_app.h"

#define DBG_TAG "dht22"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define DHT22_PIN           GET_PIN(A, 8)   /* DHT22 数据引脚：PA8（40Pin → Pin 24） */
#define DHT22_RETRY         3               /* 最大重试次数 */
#define DHT22_RETRY_DELAY   1000            /* 重试间隔：1 秒 */
#define DHT22_MIN_INTERVAL  2000            /* 最小采样间隔：2 秒（DHT22 数据手册要求） */

static dht_device_t dht_dev = RT_NULL;

static float cached_temp = 0.0f;
static float cached_humi = 0.0f;
static rt_tick_t last_read_tick = 0;

/*
 * 初始化 DHT22 室内温湿度传感器
 * 创建 DHT22 设备实例并初始化。
 * 成功返回 RT_EOK，失败返回 -RT_ERROR。
 */
int dht22_app_init(void)
{
    dht_dev = dht_create(DHT22, DHT22_PIN);
    if (dht_dev == RT_NULL)
    {
        LOG_E("DHT22 create failed on PA8");
        return -RT_ERROR;
    }

    /* DHT22 上电后需要 1 秒稳定期 */
    rt_thread_mdelay(1000);

    LOG_I("DHT22 (indoor) initialized on PA8");
    return RT_EOK;
}

/*
 * 读取 DHT22 室内温湿度数据（带重试 + 最小间隔保护）
 * DHT22 数据手册要求采样间隔 ≥ 2 秒，本函数自动跳过过频调用。
 *   temperature - 输出温度值（℃）
 *   humidity    - 输出湿度值（%）
 * 返回 1：读取成功
 * 返回 0：读取失败（返回上次缓存值）
 */
int dht22_app_read(float *temperature, float *humidity)
{
    if (dht_dev == RT_NULL)
        return 0;

    /* 最小采样间隔保护：距上次成功读取不足 2 秒则跳过 */
    rt_tick_t now = rt_tick_get();
    if (last_read_tick > 0 &&
        (now - last_read_tick) < rt_tick_from_millisecond(DHT22_MIN_INTERVAL))
    {
        *temperature = cached_temp;
        *humidity    = cached_humi;
        return 1;
    }

    for (int retry = 0; retry < DHT22_RETRY; retry++)
    {
        if (dht_read(dht_dev))
        {
            cached_temp = dht_get_temperature(dht_dev) / 10.0f;
            cached_humi = dht_get_humidity(dht_dev) / 10.0f;
            last_read_tick = now;

            *temperature = cached_temp;
            *humidity    = cached_humi;
            return 1;
        }

        if (retry < DHT22_RETRY - 1)
        {
            rt_thread_mdelay(DHT22_RETRY_DELAY);
        }
    }

    /* 多次重试失败，返回缓存值并记录告警 */
    *temperature = cached_temp;
    *humidity    = cached_humi;
    LOG_W("DHT22 retry failed");
    return 0;
}
