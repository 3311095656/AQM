#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "ap3216c.h"
#include "ap3216c_app.h"

#define DBG_TAG "ap3216c"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

static ap3216c_device_t dev_ap3216c = RT_NULL;

/*
 * 初始化 AP3216C 光照传感器
 * 挂载在 I2C2 总线上。
 * 成功返回 RT_EOK，失败返回 -RT_ERROR。
 */
int ap3216c_app_init(void)
{
    dev_ap3216c = ap3216c_init("i2c2");
    if (dev_ap3216c == RT_NULL)
    {
        LOG_E("ap3216c init failed on i2c2");
        return -RT_ERROR;
    }

    LOG_I("AP3216C (light) initialized on i2c2");
    return RT_EOK;
}

/*
 * 读取环境光照强度（AP3216C）
 * 返回光照值，单位：勒克斯（lux）
 */
float ap3216c_app_read_light(void)
{
    if (dev_ap3216c == RT_NULL)
        return 0.0f;
    return ap3216c_read_ambient_light(dev_ap3216c);
}
