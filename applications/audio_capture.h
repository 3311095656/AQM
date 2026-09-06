#ifndef APPLICATIONS_AUDIO_CAPTURE_H_
#define APPLICATIONS_AUDIO_CAPTURE_H_

#include <rtthread.h>

/* ==================== I2S 音频采集 ====================
 * 基于 STM32 HAL I2S + DMA 双缓冲，采集 16kHz/16bit 单声道 PCM。
 * 采用「ping-pong 双缓冲」：DMA 循环接收 2 秒数据，半传输中断表示
 * 前 1 秒就绪，传输完成中断表示后 1 秒就绪，各自释放信号量唤醒推理线程。
 */

/* 开关：I2S3 底层初始化已手写在 audio_capture.c（MspInit），置 1 启用采集 */
#define AUDIO_CAPTURE_ENABLE   1

/* 采集参数（与 MFCC / 训练端一致） */
#define AUDIO_SAMPLE_RATE      16000   /* 采样率 Hz */
#define AUDIO_FRAME_SAMPLES    16000   /* 1 秒 = 16000 样本 */

/* 初始化 I2S + DMA（需 CubeMX 生成的 MspInit 支持）。返回 RT_EOK / -RT_ERROR */
int audio_capture_init(void);

/* 启动 DMA 采集。返回 RT_EOK / -RT_ERROR */
int audio_capture_start(void);

/* 停止 DMA 采集。返回 RT_EOK / -RT_ERROR */
int audio_capture_stop(void);

/* 阻塞等待 1 秒音频就绪，返回该段 int16 PCM 指针；失败返回 RT_NULL */
const int16_t *audio_capture_wait_frame(void);

#endif /* APPLICATIONS_AUDIO_CAPTURE_H_ */
