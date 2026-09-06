#ifndef APPLICATIONS_VOICE_APP_H_
#define APPLICATIONS_VOICE_APP_H_

#include <rtthread.h>

/* ==================== 端侧语音识别框架 ====================
 *
 * 数据流：I2S麦克风 --DMA--> 采集线程 --环形缓冲--> 推理线程
 *         --> MFCC(CMSIS-DSP) --> Cube.AI DS-CNN --> 结果后处理 --> 设备控制
 *
 * 当前为骨架版本：I2S / CMSIS-DSP / Cube.AI 均为占位（用编译开关控制），
 * 硬件和模型到位后逐项替换即可。
 */

/* 语音指令索引。必须与训练脚本 train_dscnn.py 中 CLASS_NAMES 的
 * 前 8 类顺序完全一致（第 8/9 类 = silence/unknown，用于拒识）。
 */
typedef enum {
    VOICE_CMD_KAI_CHUANG = 0,    /* 开窗 */
    VOICE_CMD_GUAN_CHUANG,       /* 关窗 */
    VOICE_CMD_KAI_FENGSHAN,      /* 开风扇 */
    VOICE_CMD_GUAN_FENGSHAN,     /* 关风扇 */
    VOICE_CMD_ZHILENG,           /* 制冷 */
    VOICE_CMD_ZHIRE,             /* 制热 */
    VOICE_CMD_JIASHI,            /* 加湿 */
    VOICE_CMD_CHUSHI,            /* 除湿 */
    VOICE_NUM_CMDS               /* 有效指令数（8），>= 此值视为静音/未知 */
} voice_cmd_t;

/* 初始化语音识别模块。硬件/模型未就绪时安全返回，不启动线程。 */
int voice_app_init(void);

/* 语音识别是否已启用（I2S 采集 + 推理均就绪）。 */
int voice_is_enabled(void);

#endif /* APPLICATIONS_VOICE_APP_H_ */
