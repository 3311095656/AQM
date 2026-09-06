/*
 * audio_test.c —— I2S 采集通路验证（不依赖模型）
 *
 * 用途：KWS_MODEL_ENABLE=0 时单独验证「麦克风 → I2S+DMA → 双缓冲 → PCM」
 *       是否正常工作。通过 msh 命令观察波形幅值/电平分布/节拍。
 *
 * 判读标准：
 *   audio_start 后每约 1 秒打一行统计：
 *   - 环境安静：峰值应在几十~几百之间（本底噪声），均值接近 0
 *   - 对麦克风说话：峰值应明显增大（数千以上），均值绝对值也变大
 *   - 长时间全 0：接线错误（SD/CK/WS 接反、没接 3.3V）或采集未跑起来
 *   - 峰值恒为 32767/-32768 饱和：增益异常或声道交织未抽取
 *
 * 命令：
 *   audio_start   启动采集并开始每秒打印统计
 *   audio_dump    打印最近 1 秒 PCM 的头部 32 个原始样本
 *   audio_stop    停止采集
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "audio_capture.h"

#if AUDIO_CAPTURE_ENABLE

#define DBG_TAG "audio_test"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 每秒统计用的样本缓冲（复用最近就绪帧） */
static const int16_t *g_last_frame = RT_NULL;
static volatile int g_dump_req = 0;

/* 采集线程：等帧 -> 统计 -> 打印 */
static void audio_stat_thread_entry(void *param)
{
    (void)param;
    LOG_I("audio stat thread started, watching frames every ~1s");

    while (1)
    {
        const int16_t *pcm = audio_capture_wait_frame();
        if (pcm == RT_NULL)
            continue;

        g_last_frame = pcm;

        int16_t peak = 0;
        long long sum = 0;
        int zeros = 0;
        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++)
        {
            int16_t v = pcm[i];
            int16_t a = (v >= 0) ? v : (int16_t)(-v);
            if (a > peak) peak = a;
            sum += v;
            if (v == 0) zeros++;
        }

        if (g_dump_req)
        {
            g_dump_req = 0;
            rt_kprintf("dump first 32 samples:\n");
            for (int i = 0; i < 32; i++)
            {
                rt_kprintf("%6d ", pcm[i]);
                if ((i & 7) == 7)
                    rt_kprintf("\n");
            }
        }

        /* 1 秒 16000 样本统计一行 */
        rt_kprintf("audio: peak=%6d avg=%6ld zeros=%d/16000\n",
                   peak, (long)(sum / AUDIO_FRAME_SAMPLES), zeros);
    }
}

/* ==================== msh 命令 ==================== */

static rt_thread_t g_stat_tid = RT_NULL;

static void msh_audio_start(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (g_stat_tid != RT_NULL)
    {
        rt_kprintf("already running\n");
        return;
    }
    if (audio_capture_init() != RT_EOK)
    {
        rt_kprintf("audio_capture_init FAILED\n");
        return;
    }
    if (audio_capture_start() != RT_EOK)
    {
        rt_kprintf("audio_capture_start FAILED\n");
        return;
    }
    g_stat_tid = rt_thread_create("audiotest", audio_stat_thread_entry, RT_NULL,
                                  2048, 20, 5);
    if (g_stat_tid == RT_NULL)
    {
        rt_kprintf("thread create failed\n");
        return;
    }
    rt_thread_startup(g_stat_tid);
    rt_kprintf("capture started, watch 'audio: peak=...' lines every ~1s\n");
}
MSH_CMD_EXPORT(msh_audio_start, start I2S capture and print per-second stats);

static void msh_audio_dump(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (g_last_frame == RT_NULL)
    {
        rt_kprintf("no frame yet, run audio_start first\n");
        return;
    }
    g_dump_req = 1;
    rt_kprintf("dump requested\n");
}
MSH_CMD_EXPORT(msh_audio_dump, dump first 32 samples of last frame);

static void msh_audio_stop(int argc, char *argv[])
{
    (void)argc; (void)argv;
    audio_capture_stop();
    if (g_stat_tid != RT_NULL)
    {
        /* 线程阻塞在信号量上无法删除，只停采集，下次 start 复用 */
        rt_kprintf("capture stopped (stat thread stays idle)\n");
        return;
    }
    rt_kprintf("capture stopped\n");
}
MSH_CMD_EXPORT(msh_audio_stop, stop I2S capture);

#endif /* AUDIO_CAPTURE_ENABLE */
