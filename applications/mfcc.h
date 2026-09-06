#ifndef APPLICATIONS_MFCC_H_
#define APPLICATIONS_MFCC_H_

#include <stdint.h>

/* 开关：已启用 CMSIS-DSP 库后置 1，用 arm_rfft_fast_f32 加速 FFT。
 * 置 0 时用自包含纯 C FFT（无需任何依赖）。两者缩放一致（均为未归一化 FFT），
 * 数值等价，仅性能不同。 */
#define MFCC_USE_CMSIS_DSP   0

/* ==================== MFCC 参数 ====================
 * 这些参数必须与训练脚本 train_dscnn.py 中的 MFCC 参数严格一致，
 * 否则训练/部署特征域不一致，识别精度会明显下降。
 */

#define MFCC_SAMPLE_RATE   16000   /* 采样率 Hz */
#define MFCC_SAMPLE_LEN    16000   /* 1 秒 = 16000 样本 */
#define MFCC_FRAME_LEN     400     /* 帧长 25ms @16k */
#define MFCC_FRAME_STEP    160     /* 帧移 10ms @16k */
#define MFCC_NUM_FRAMES    98      /* 1 秒帧数 = 1 + (16000-400)/160 */
#define MFCC_NUM_COEFFS    40      /* MFCC 维数 */
#define MFCC_FFT_LEN       512     /* FFT 点数 */
#define MFCC_LOW_HZ        20      /* Mel 滤波器下限 Hz */
#define MFCC_HIGH_HZ       8000    /* Mel 滤波器上限 Hz */
#define MFCC_PREEMPH       0.97f   /* 预加重系数 */

/* 初始化 MFCC：预计算 Hann 窗、Mel 滤波器组参数、DCT-II 矩阵。
 * 返回 0 成功，负值失败。 */
int mfcc_init(void);

/* 提取 MFCC。
 * pcm:      输入，16000 个 int16 样本（16kHz 单声道，1 秒）
 * features: 输出，98*40 个 float，按行存储：features[frame*40 + coeff]
 * 返回 0 成功，负值失败。 */
int mfcc_extract(const int16_t *pcm, float *features);

#endif /* APPLICATIONS_MFCC_H_ */
