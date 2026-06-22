#ifndef APPLICATIONS_AC_APP_H_
#define APPLICATIONS_AC_APP_H_

#include <rtthread.h>

/* ==================== 空调控制模块 ==================== */

/*
 * 空调工作模式
 * AC_OFF:     关闭
 * AC_COOL:    制冷（室温 > 目标温度时开启）
 * AC_HEAT:    制热（室温 < 目标温度时开启）
 * AC_AUTO:    自动（根据温差自动选择制冷/制热）
 */
typedef enum {
    AC_OFF = 0,
    AC_COOL,
    AC_HEAT,
    AC_AUTO
} ac_mode_t;

/* 初始化空调模块（配置 GPIO，初始关闭） */
int ac_app_init(void);

/* 设置空调模式 */
void ac_set_mode(ac_mode_t mode);

/* 获取当前空调模式 */
ac_mode_t ac_get_mode(void);

/* 空调是否在运行（制冷或制热） */
int ac_is_running(void);

/* 自动温控：根据当前温度和目标温度自动调节（1秒调用一次） */
/* 返回 1 表示状态变化，0 表示无变化 */
int ac_auto_control(float current_temp, float target);

/* 目标温度 getter/setter（范围 16~40°C，步长 0.5°C） */
float ac_get_target_temp(void);
float ac_set_target_temp(float temp);

/* 手动/自动模式标志（MQTT 远程控制使用） */
int ac_manual_mode(void);
void ac_set_manual(int manual);

/* 强制关闭（燃气报警时调用） */
void ac_force_off(void);

#endif /* APPLICATIONS_AC_APP_H_ */
