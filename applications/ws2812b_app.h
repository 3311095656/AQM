#ifndef APPLICATIONS_WS2812B_APP_H_
#define APPLICATIONS_WS2812B_APP_H_

#include <rtthread.h>

/* ==================== WS2812B RGB LED 驱动 ==================== */

/* LED 编号 */
#define WS2812B_LED_HUMI        0       /* 第1颗：加湿/除湿状态 LED */
#define WS2812B_LED_AC          1       /* 第2颗：空调状态 LED */
#define WS2812B_LED_NUM         2       /* 当前 2 颗 LED */

/* 颜色定义 (R, G, B) */
#define COLOR_OFF               0, 0, 0         /* 灭 */
#define COLOR_BLUE              0, 0, 255       /* 蓝色：制冷 */
#define COLOR_RED               255, 0, 0       /* 红色：制热 */
#define COLOR_GREEN             0, 255, 0       /* 绿色：自动/待机 */
#define COLOR_MAGENTA           255, 0, 255     /* 紫色：加湿 */
#define COLOR_CYAN              0, 255, 255     /* 青色：除湿 */

/* 初始化 WS2812B（配置数据引脚为输出，全部熄灭） */
int ws2812b_app_init(void);

/* 设置某颗 LED 的颜色 */
void ws2812b_set_color(rt_uint8_t led, rt_uint8_t r, rt_uint8_t g, rt_uint8_t b);

/* 熄灭某颗 LED */
void ws2812b_set_off(rt_uint8_t led);

/* 全部熄灭 */
void ws2812b_all_off(void);

#endif /* APPLICATIONS_WS2812B_APP_H_ */
