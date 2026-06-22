#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "fan_app.h"

#define FAN_INA  GET_PIN(G, 2)   /* L9110S INA（PG2） */
#define FAN_INB  GET_PIN(G, 3)   /* L9110S INB（PG3） */

static int fan_state = 0;

int fan_app_init(void)
{
    rt_pin_mode(FAN_INA, PIN_MODE_OUTPUT);
    rt_pin_mode(FAN_INB, PIN_MODE_OUTPUT);
    rt_pin_write(FAN_INA, PIN_LOW);
    rt_pin_write(FAN_INB, PIN_LOW);
    fan_state = 0;
    return RT_EOK;
}

void fan_on(void)
{
    fan_state = 1;
    rt_pin_write(FAN_INA, PIN_HIGH);
    rt_pin_write(FAN_INB, PIN_LOW);
}

void fan_off(void)
{
    fan_state = 0;
    rt_pin_write(FAN_INA, PIN_LOW);
    rt_pin_write(FAN_INB, PIN_LOW);
}

int fan_is_on(void)
{
    return fan_state;
}

#ifdef FINSH_USING_MSH
#include <finsh.h>

static void msh_fan_on(int argc, char *argv[])
{
    fan_on();
    rt_kprintf("fan: ON\n");
}
MSH_CMD_EXPORT(msh_fan_on, turn on the fan);

static void msh_fan_off(int argc, char *argv[])
{
    fan_off();
    rt_kprintf("fan: OFF\n");
}
MSH_CMD_EXPORT(msh_fan_off, turn off the fan);
#endif
