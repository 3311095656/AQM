#ifndef __FAN_APP_H__
#define __FAN_APP_H__

#include <rtthread.h>

int fan_app_init(void);
void fan_on(void);
void fan_off(void);
int fan_is_on(void);

#endif
