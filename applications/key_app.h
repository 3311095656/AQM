#ifndef __KEY_APP_H__
#define __KEY_APP_H__

#include <rtthread.h>

/* 初始化所有按键引脚 */
void key_app_init(void);

/* KEY_LEFT（PC0）按下返回 1，未按返回 0 */
int key_left_is_pressed(void);

/* KEY1（PC1）按下返回 1，未按返回 0 */
int key1_is_pressed(void);

/* KEY2（PC2）按下返回 1，未按返回 0 */
int key2_is_pressed(void);

#endif
