#ifndef __NTP_APP_H__
#define __NTP_APP_H__

#include <rtthread.h>

/* NTP 时间同步函数
 * 连接 NTP 服务器获取当前时间，成功后更新系统时钟。
 * 返回 0 表示成功，-1 表示失败。
 */
int ntp_app_sync(void);

#endif
