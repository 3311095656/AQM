/*
 * kws_model.c —— STM32Cube.AI (X-CUBE-AI) 推理调用封装
 *
 * 职责：
 *   1. 创建/初始化 Cube.AI 网络，读取输入输出 int8 量化参数(scale/zero_point)；
 *   2. 推理时把 float MFCC 特征量化为 int8 送入，输出 int8 反量化回 float 概率。
 *
 * ====================== 接入步骤（KWS_MODEL_ENABLE 置 1 之前） ======================
 * 1. CubeMX 的 X-CUBE-AI 面板导入 kws_dscnn_int8.tflite，网络名填 kws_dscnn，
 *    Analyze 验证后 Generate Code；
 * 2. 把生成的 kws_dscnn.c/h、kws_dscnn_data.c/h 拷入 applications/ 目录
 *    （本目录 SConscript 用 Glob('*.c') 自动编译，头文件搜索路径已含本目录）；
 * 3. 拷贝 AI 运行库：X-CUBE-AI 安装包内 Middlewares/ST/ai_runtime/ 下的
 *    Include/（运行库头文件）与 Lib/GCC/（libai_runtime.a、libcmsis_nn.a 等），
 *    放入工程并在工程属性中添加 include 路径与链接库（详见 voice_kws_checklist.md 阶段2）；
 * 4. 编译。若报 undefined reference，按下方【符号对照表】核对名字；
 * 5. 将 kws_model.h 中 KWS_MODEL_ENABLE 置 1。
 * ================================================================================
 */

#include <stddef.h>
#include <math.h>
#include "kws_model.h"

#if KWS_MODEL_ENABLE

/* ========================= 符号对照表 =========================
 * 网络名 = kws_dscnn（CubeMX Add network 时填的名字）。
 * 若名字不同，把本文件中所有 "kws_dscnn" 全局替换即可。
 *
 *   头文件      kws_dscnn.h / kws_dscnn_data.h
 *   初始化      ai_kws_dscnn_create_and_init(&net, acts, NULL)
 *   IO 描述符   ai_kws_dscnn_inputs_get(net, NULL) / ai_kws_dscnn_outputs_get(net, NULL)
 *   推理        ai_kws_dscnn_run(net, &in_buf, &out_buf)，返回 batch 数（应为 1）
 *   网络报告    ai_kws_dscnn_get_report(net, &report)（6.x 旧版叫 ai_kws_dscnn_get_info）
 *   大小宏      AI_KWS_DSCNN_DATA_ACTIVATIONS_SIZE（6.x 旧版叫 ..._ACTIVATION_1_SIZE）
 *               AI_KWS_DSCNN_IN_1_SIZE_BYTES / AI_KWS_DSCNN_OUT_1_SIZE_BYTES
 * ============================================================= */
#include "kws_dscnn.h"
#include "kws_dscnn_data.h"

/* 网络句柄与 IO buffer 描述符。
 * data 字段在每次推理前指向我们自己的输入/输出缓冲。 */
static ai_handle  g_net  = AI_HANDLE_NULL;
static ai_buffer *g_ain  = NULL;
static ai_buffer *g_aout = NULL;

/* 激活内存：放主 SRAM；紧张时可移到 CCM（链接脚本加 .ccmram 段后改 section 属性）。
 * 大小由 Cube.AI 分析报告给出。 */
AI_ALIGNED(32)
static ai_u8 g_activations[AI_KWS_DSCNN_DATA_ACTIVATIONS_SIZE];

/* int8 输入/输出原始 buffer（int8 模型下字节数 == 元素数） */
AI_ALIGNED(32)
static ai_i8 g_in[AI_KWS_DSCNN_IN_1_SIZE_BYTES];    /* = 98*40 = 3920 */
AI_ALIGNED(32)
static ai_i8 g_out[AI_KWS_DSCNN_OUT_1_SIZE_BYTES];  /* = 8 */

/* 量化参数 */
static float g_in_scale  = 1.0f, g_in_zero  = 0.0f;
static float g_out_scale = 1.0f, g_out_zero = 0.0f;

/* 量化参数获取方式（二选一）：
 * 1 = 运行时从网络报告读取。不同 X-CUBE-AI 版本字段名略有差异，编译报错时
 *     对照运行库 ai_datatypes_generics.h 中 ai_network_report / ai_quantization_params
 *     的实际定义微调。
 * 0 = 编译期硬编码（已从 kws_dscnn_int8.tflite 提取，推荐，跨版本免维护）：
 *     输入 [1,98,40,1] int8  scale=0.09307891  zp=+88
 *     输出 [1,10]      int8  scale=0.00390625  zp=-128  (即 softmax 概率 q/256) */
#define KWS_QUANT_FROM_REPORT   0

#define KWS_IN_SCALE    0.09307891f
#define KWS_IN_ZEROPT   88.0f
#define KWS_OUT_SCALE   0.00390625f
#define KWS_OUT_ZEROPT  (-128.0f)

int kws_model_init(void)
{
    ai_error err;
    ai_network_report report;
    const ai_handle acts[] = { g_activations };

    /* 1) 创建并初始化网络（权重取自 kws_dscnn_data.c，激活内存由我们提供） */
    err = ai_kws_dscnn_create_and_init(&g_net, acts, NULL);
    if (err.type != AI_ERROR_NONE)
        return -1;

    /* 2) 拿到 IO buffer 描述符（数量 AI_KWS_DSCNN_IN_NUM / OUT_NUM，本模型均为 1） */
    g_ain  = ai_kws_dscnn_inputs_get(g_net, NULL);
    g_aout = ai_kws_dscnn_outputs_get(g_net, NULL);
    if (g_ain == NULL || g_aout == NULL)
        return -1;

    /* 3) 量化参数（int8 模型必需） */
#if KWS_QUANT_FROM_REPORT
    if (!ai_kws_dscnn_get_report(g_net, &report))
        return -1;
    g_in_scale  = report.inputs[0].quantization.scale[0];
    g_in_zero   = report.inputs[0].quantization.zero_point[0];
    g_out_scale = report.outputs[0].quantization.scale[0];
    g_out_zero  = report.outputs[0].quantization.zero_point[0];
#else
    g_in_scale  = KWS_IN_SCALE;     /* TODO: 替换为实际输入 scale */
    g_in_zero   = KWS_IN_ZEROPT;    /* TODO: 替换为实际输入 zero_point（常见 0 或 -128） */
    g_out_scale = KWS_OUT_SCALE;    /* TODO: 替换为实际输出 scale */
    g_out_zero  = KWS_OUT_ZEROPT;   /* TODO: 替换为实际输出 zero_point */
#endif

    return 0;
}

int kws_model_run(const float *features, float *probs)
{
    ai_i32 batch;
    int i;

    /* 1) float -> int8：q = round(f/scale) + zero_point，钳位 [-128,127]
     *    特征布局 features[frame*40 + coeff] 与模型 NHWC 输入 [1,98,40,1] 一致，无需转置 */
    for (i = 0; i < KWS_INPUT_SIZE; i++)
    {
        float q = features[i] / g_in_scale + g_in_zero;
        if (q > 127.0f)
            q = 127.0f;
        else if (q < -128.0f)
            q = -128.0f;
        g_in[i] = (ai_i8)lrintf(q);
    }

    /* 2) 推理：把 IO 描述符的 data 指到我们的 buffer */
    g_ain[0].data  = AI_HANDLE_PTR(g_in);
    g_aout[0].data = AI_HANDLE_PTR(g_out);
    batch = ai_kws_dscnn_run(g_net, &g_ain[0], &g_aout[0]);
    if (batch != 1)
        return -1;

    /* 3) int8 -> float：f = (q - zero_point) * scale */
    for (i = 0; i < KWS_OUTPUT_SIZE; i++)
        probs[i] = ((float)g_out[i] - g_out_zero) * g_out_scale;

    return 0;
}

#else /* !KWS_MODEL_ENABLE */

/* 占位实现：未接入 Cube.AI 生成代码时，直接失败/全零 */
int kws_model_init(void)
{
    return -1;
}

int kws_model_run(const float *features, float *probs)
{
    (void)features;
    for (int i = 0; i < KWS_OUTPUT_SIZE; i++)
        probs[i] = 0.0f;
    return 0;
}

#endif /* KWS_MODEL_ENABLE */
