#ifndef APPLICATIONS_KWS_MODEL_H_
#define APPLICATIONS_KWS_MODEL_H_

#include <stdint.h>

/* ==================== KWS 模型推理封装 ====================
 * 封装 STM32Cube.AI 生成的 DS-CNN 推理代码，对外只暴露 init/run 两个接口。
 * voice_app.c 通过本模块调用模型，无需关心 Cube.AI 内部细节。
 */

/* 开关：已用 STM32Cube.AI 生成并放入工程后置 1 */
#define KWS_MODEL_ENABLE   0

/* 模型输入输出维度（与训练脚本 train_dscnn.py 一致，需与生成结果核对） */
#define KWS_INPUT_SIZE     (98 * 40)   /* MFCC 特征：98 帧 x 40 维 */
#define KWS_OUTPUT_SIZE    10          /* 8 指令 + silence + unknown */

/* 初始化模型：创建并初始化 Cube.AI 网络，读取量化参数。返回 0 成功。 */
int kws_model_init(void);

/* 推理：float 特征 -> int8 量化 -> 推理 -> 反量化 -> float 概率。
 * features: 输入，KWS_INPUT_SIZE 个 float
 * probs:    输出，KWS_OUTPUT_SIZE 个 float（softmax 概率）
 * 返回 0 成功，负值失败。 */
int kws_model_run(const float *features, float *probs);

#endif /* APPLICATIONS_KWS_MODEL_H_ */
