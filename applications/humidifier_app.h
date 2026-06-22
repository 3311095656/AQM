#ifndef APPLICATIONS_HUMIDIFIER_APP_H_
#define APPLICATIONS_HUMIDIFIER_APP_H_

#include <rtthread.h>

/* ==================== 加湿/除湿控制模块 ==================== */

/*
 * 加湿器/除湿器工作模式
 * HUMI_OFF:        关闭
 * HUMI_HUMIDIFY:   加湿（湿度低于目标时开启）
 * HUMI_DEHUMIDIFY: 除湿（湿度高于目标时开启）
 * HUMI_AUTO:       自动（根据湿度差自动选择加湿/除湿）
 */
typedef enum {
    HUMI_OFF = 0,
    HUMI_HUMIDIFY,
    HUMI_DEHUMIDIFY,
    HUMI_AUTO
} humi_mode_t;

/* 初始化加湿/除湿模块（配置 GPIO，初始关闭） */
int humidifier_app_init(void);

/* 设置模式 */
void humi_set_mode(humi_mode_t mode);

/* 获取当前模式 */
humi_mode_t humi_get_mode(void);

/* 是否在运行（加湿或除湿） */
int humi_is_running(void);

/* 自动湿度控制：根据当前湿度和目标湿度自动调节（1秒调用一次） */
/* 返回 1 表示状态变化，0 表示无变化 */
int humi_auto_control(float current_humi, float target_humi);

/* 目标湿度 getter/setter（范围 20~90%，步长 5%） */
float humi_get_target_humi(void);
float humi_set_target_humi(float humi);

/* 手动/自动模式标志 */
int humi_manual_mode(void);
void humi_set_manual(int manual);

/* 强制关闭（燃气报警时调用） */
void humi_force_off(void);

#endif /* APPLICATIONS_HUMIDIFIER_APP_H_ */
