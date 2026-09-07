/*
 * voice_app.c —— 端侧语音识别线程框架
 *
 * 数据流：
 *   I2S麦克风 --DMA双缓冲--> 推理线程 --> MFCC(mfcc.c) --> Cube.AI(kws_model.c)
 *     --> 置信度阈值/拒识/去抖 --> 设备控制
 *
 * 状态：
 *   - MFCC（mfcc.c）、Cube.AI 推理（kws_model.c）、结果后处理、指令映射 均已实现；
 *   - I2S 采集见 audio_capture.c（需 CubeMX 配置 I2S+DMA 及硬件接入）。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#include "voice_app.h"
#include "fan_app.h"
#include "servo_app.h"
#include "humidifier_app.h"
#include "ac_app.h"
#include "mfcc.h"
#include "kws_model.h"
#include "audio_capture.h"

#define DBG_TAG "voice"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ==================== 结果后处理参数 ==================== */
#define VOICE_CONF_THRESHOLD     0.85f   /* 置信度阈值，低于此值丢弃 */
#define VOICE_DEBOUNCE_CNT       2       /* 连续命中 N 次才触发 */
#define VOICE_TRIG_INTERVAL_MS   1000    /* 同一指令重复触发最小间隔 */

/* ==================== 推理线程配置 ==================== */
#define VOICE_INFER_STACK    8192
#define VOICE_INFER_PRIO     15

/* 模型类别数：6 指令 + silence + unknown */
#define VOICE_NUM_CLASSES    (VOICE_NUM_CMDS + 2)

/* MFCC 特征图与推理输出（静态分配，不放线程栈）。
 * 特征图约 15.7KB 放 CCM(RAM2) 的 .ccm_bss 段（CPU-only 访问），为主 SRAM 腾空间。 */
#define VOICE_CCM __attribute__((section(".ccm_bss")))
static VOICE_CCM float g_mfcc[MFCC_NUM_FRAMES * MFCC_NUM_COEFFS];
static float g_probs[VOICE_NUM_CLASSES];

static int g_enabled = 0;   /* 采集 + 推理是否均就绪 */

/* ==================== 指令 → 设备动作映射表 ==================== */

static void cmd_window_open(void)   { servo_window_open(); }
static void cmd_window_close(void)  { servo_window_close(); }
static void cmd_fan_on(void)        { fan_on(); }
static void cmd_fan_off(void)       { fan_off(); }
static void cmd_ac_cool(void)       { ac_set_mode(AC_COOL); }
static void cmd_ac_heat(void)       { ac_set_mode(AC_HEAT); }
static void cmd_humi_humidify(void) { humi_set_mode(HUMI_HUMIDIFY); }
static void cmd_humi_dehumidify(void){ humi_set_mode(HUMI_DEHUMIDIFY); }

typedef struct {
    const char *name;      /* 指令名（与训练类别一致，便于日志） */
    void (*action)(void);  /* 执行动作 */
} voice_action_t;

/* 顺序必须与 voice_app.h 的 voice_cmd_t / train_dscnn.py 的 CLASS_NAMES 一致 */
static const voice_action_t g_actions[VOICE_NUM_CMDS] = {
    { "kai_chuang",    cmd_window_open     },
    { "guan_chuang",   cmd_window_close    },
    { "kai_fengshan",  cmd_fan_on          },
    { "guan_fengshan", cmd_fan_off         },
    { "zhileng",       cmd_ac_cool         },
    { "zhire",         cmd_ac_heat         },
    { "jiashi",        cmd_humi_humidify   },
    { "chushi",        cmd_humi_dehumidify },
};

/* ==================== MFCC 特征提取（自包含实现，见 mfcc.c） ==================== */

static int voice_mfcc_extract(const int16_t *pcm)
{
    if (pcm == RT_NULL)
        return -RT_ERROR;
    return mfcc_extract(pcm, g_mfcc);
}

/* ==================== Cube.AI 推理（封装见 kws_model.c） ==================== */

static int voice_model_run(void)
{
    return kws_model_run(g_mfcc, g_probs);
}

/* ==================== 结果后处理（完整逻辑） ==================== */

static int voice_argmax(const float *p, int n)
{
    int i, best = 0;
    for (i = 1; i < n; i++)
    {
        if (p[i] > p[best])
            best = i;
    }
    return best;
}

/*
 * 处理一次推理结果：
 *   1) 置信度阈值过滤；
 *   2) 未知类别(silence/unknown)拒识；
 *   3) 连续命中去抖；
 *   4) 重复触发抑制（时间间隔）。
 * 全部通过后才执行对应设备动作。
 */
static void voice_handle_result(void)
{
    static int   last_cmd = -1;
    static int   confirm_cnt = 0;
    static rt_tick_t last_trigger = 0;

    int   best = voice_argmax(g_probs, VOICE_NUM_CLASSES);
    float conf = g_probs[best];

    /* 1) 置信度不足 -> 丢弃 */
    if (conf < VOICE_CONF_THRESHOLD)
        return;

    /* 2) 静音/未知类 -> 拒识（不做任何动作） */
    if (best >= VOICE_NUM_CMDS)
    {
        last_cmd = -1;
        confirm_cnt = 0;
        return;
    }

    /* 3) 去抖：连续命中同一指令才有效 */
    if (best == last_cmd)
    {
        confirm_cnt++;
    }
    else
    {
        last_cmd = best;
        confirm_cnt = 1;
    }
    if (confirm_cnt < VOICE_DEBOUNCE_CNT)
        return;

    /* 4) 重复触发抑制 */
    rt_tick_t now = rt_tick_get();
    if (now - last_trigger < rt_tick_from_millisecond(VOICE_TRIG_INTERVAL_MS))
        return;
    last_trigger = now;

    /* 执行动作 */
    LOG_I("voice command: [%s] conf=%.2f", g_actions[best].name, conf);
    g_actions[best].action();
}

/* ==================== 推理线程 ==================== */

static void voice_infer_thread_entry(void *parameter)
{
    const int16_t *pcm;

    LOG_I("voice infer thread started");

    while (1)
    {
        /* 等待 1 秒音频就绪（audio_capture 内部信号量） */
        pcm = audio_capture_wait_frame();
        if (pcm == RT_NULL)
            continue;

        /* MFCC 特征提取 */
        if (voice_mfcc_extract(pcm) != RT_EOK)
            continue;

        /* Cube.AI 推理 */
        if (voice_model_run() != RT_EOK)
            continue;

        /* 结果后处理 + 设备控制 */
        voice_handle_result();
    }
}

/* ==================== 初始化 ==================== */

int voice_is_enabled(void)
{
    return g_enabled;
}

int voice_app_init(void)
{
    /* 模型未接入（KWS_MODEL_ENABLE=0）时：
     * - 不启动推理线程（kws_model 占位实现无意义）；
     * - 采集通路验证走 msh 命令 audio_start（见 audio_test.c），这里直接跳过。 */
    if (!KWS_MODEL_ENABLE)
    {
        LOG_W("model not enabled, voice inference skipped "
              "(use 'audio_start' to verify I2S capture path alone)");
        return RT_EOK;
    }

    if (!AUDIO_CAPTURE_ENABLE)
    {
        LOG_W("audio capture not enabled, voice init skipped");
        return RT_EOK;
    }

    /* 初始化 MFCC（预计算 Hann 窗 / Mel 滤波器组 / DCT 矩阵） */
    if (mfcc_init() != 0)
    {
        LOG_E("mfcc init failed");
        return -RT_ERROR;
    }

    /* 初始化模型（Cube.AI） */
    if (kws_model_init() != 0)
    {
        LOG_E("kws model init failed");
        return -RT_ERROR;
    }

    /* 初始化并启动 I2S 采集 */
    if (audio_capture_init() != RT_EOK)
    {
        LOG_E("audio capture init failed");
        return -RT_ERROR;
    }
    if (audio_capture_start() != RT_EOK)
    {
        LOG_E("audio capture start failed");
        return -RT_ERROR;
    }

    /* 启动推理线程 */
    rt_thread_t infer_tid = rt_thread_create("vinfer",
                                              voice_infer_thread_entry,
                                              RT_NULL,
                                              VOICE_INFER_STACK,
                                              VOICE_INFER_PRIO,
                                              5);
    if (!infer_tid)
    {
        LOG_E("voice infer thread create failed");
        return -RT_ERROR;
    }
    rt_thread_startup(infer_tid);

    g_enabled = 1;
    LOG_I("voice recognition enabled");
    return RT_EOK;
}

/* ==================== 调试命令 ==================== */

#ifdef FINSH_USING_MSH
#include <finsh.h>

/* 手动触发某条指令（用于无硬件时验证后处理/映射逻辑） */
static void msh_voice_trigger(int argc, char *argv[])
{
    if (argc < 2)
    {
        rt_kprintf("usage: voice_trigger <cmd_index 0..%d>\n", VOICE_NUM_CMDS - 1);
        return;
    }
    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= VOICE_NUM_CMDS)
    {
        rt_kprintf("invalid index\n");
        return;
    }
    /* 构造一个 100% 置信度的结果，直接走一次后处理 */
    memset(g_probs, 0, sizeof(g_probs));
    g_probs[idx] = 1.0f;
    voice_handle_result();
}
MSH_CMD_EXPORT(msh_voice_trigger, manually trigger a voice command);

static void msh_voice_status(int argc, char *argv[])
{
    rt_kprintf("voice enabled: %d\n", g_enabled);
    rt_kprintf("Capture=%d Model=%d\n", AUDIO_CAPTURE_ENABLE, KWS_MODEL_ENABLE);
}
MSH_CMD_EXPORT(msh_voice_status, show voice module status);
#endif
