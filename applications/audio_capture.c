/*
 * audio_capture.c —— I2S + DMA 双缓冲音频采集
 *
 * 数据流：
 *   I2S麦克风 --I2S主接收--> DMA循环模式 --> g_pcm[2][16000]（ping-pong 双缓冲）
 *     --半传输中断(前1秒就绪)/传输完成中断(后1秒就绪)--> 释放信号量
 *     --> audio_capture_wait_frame() 阻塞返回就绪的 1 秒 PCM
 *
 * 说明：
 *   - 本文件只负责「采集逻辑」（双缓冲 + 信号量 + HAL 调用 + 回调）；
 *   - GPIO 复用、DMA 通道、NVIC、PLLI2S 时钟树 这些硬件底层，请用 CubeMX
 *     配置 I2S（主接收、16kHz、16bit、Philips 标准）并生成，然后按下方
 *     "MspInit" 注释把生成代码合并进来。
 *   - 默认麦克风按 I2S 数字麦（如 INMP441）单声道处理：DMA 直接收到
 *     int16 单声道 PCM。若你的麦克风输出左右声道交织，需在回调里做
 *     左声道抽取（见 HAL_I2S_RxHalfCpltCallback 内的 TODO）。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "audio_capture.h"

#if AUDIO_CAPTURE_ENABLE

/* ---- I2S3 句柄 + DMA 句柄（手写初始化，不依赖 CubeMX 生成） ---- */
static I2S_HandleTypeDef hi2s3;          /* I2S3 句柄 */
static DMA_HandleTypeDef hdma_spi3_rx;   /* I2S3 接收 DMA 句柄 */

/* ---- 双缓冲：2 秒 = 2 * 16000 个 int16 样本（共 64000 字节） ---- */
static int16_t g_pcm[2][AUDIO_FRAME_SAMPLES];

/* 就绪段索引 + 同步信号量 */
static volatile int g_ready_index = 0;
static struct rt_semaphore g_frame_ready;

/* ==================== 中断服务函数 ====================
 * 启动文件里这两个 IRQHandler 是弱定义（默认死循环在 Default_Handler），
 * 手写 MspInit 绕过 CubeMX 后必须自己提供，否则 DMA 半传输中断一触发就卡死。
 */
void DMA1_Stream0_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&hdma_spi3_rx);
    rt_interrupt_leave();
}

/* I2S 错误中断（HAL 使能了 ERR 中断，溢出/下溢时触发） */
void SPI3_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_I2S_IRQHandler(&hi2s3);
    rt_interrupt_leave();
}

/* ==================== DMA 中断回调（中断上下文） ==================== */

/* 半传输完成：前 1 秒（g_pcm[0]）就绪 */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
    /* TODO: 若麦克风为左右声道交织输出，在此从 g_pcm[0] 抽取左声道 */
    g_ready_index = 0;
    rt_sem_release(&g_frame_ready);
}

/* 传输完成：后 1 秒（g_pcm[1]）就绪 */
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
    /* TODO: 若麦克风为左右声道交织输出，在此从 g_pcm[1] 抽取左声道 */
    g_ready_index = 1;
    rt_sem_release(&g_frame_ready);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
    /* 采集出错：可在此重启采集或置错误标志 */
}

/* ==================== 初始化 ==================== */

static uint8_t g_inited = 0;   /* 防止重复 init（HAL_I2S_Init 重复调用会卡 State） */

int audio_capture_init(void)
{
    if (g_inited)
        return RT_EOK;

    /* 1) 信号量初始化 */
    if (rt_sem_init(&g_frame_ready, "audio_frame", 0, RT_IPC_FLAG_FIFO) != RT_EOK)
        return -RT_ERROR;

    /* 2) 配置 I2S 参数（16kHz / 16bit / Philips / 主接收） */
    hi2s3.Instance = SPI3;
    hi2s3.Init.Mode = I2S_MODE_MASTER_RX;
    hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;   /* INMP441 等无需 MCLK */
    hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_16K;
    hi2s3.Init.CPOL = I2S_CPOL_LOW;
    hi2s3.Init.ClockSource = I2S_CLOCK_PLL;           /* 时钟树由 CubeMX 配置 */

    if (HAL_I2S_Init(&hi2s3) != HAL_OK)
        return -RT_ERROR;

    g_inited = 1;
    return RT_EOK;
}

int audio_capture_start(void)
{
    if (!g_inited)
        return -RT_ERROR;
    /* DMA 循环接收 2 秒（32000 个 16bit 样本），半传输/传输完成触发回调 */
    if (HAL_I2S_Receive_DMA(&hi2s3, (uint16_t *)&g_pcm[0][0],
                            AUDIO_FRAME_SAMPLES * 2) != HAL_OK)
        return -RT_ERROR;
    return RT_EOK;
}

int audio_capture_stop(void)
{
    if (HAL_I2S_DMAStop(&hi2s3) != HAL_OK)
        return -RT_ERROR;
    return RT_EOK;
}

const int16_t *audio_capture_wait_frame(void)
{
    if (rt_sem_take(&g_frame_ready, RT_WAITING_FOREVER) != RT_EOK)
        return RT_NULL;
    return g_pcm[g_ready_index];
}

/*
 * HAL_I2S_MspInit —— I2S3 底层初始化（手写，绕过 CubeMX）
 * 完成：PLLI2S 时钟、GPIO 复用、DMA 配置、NVIC 中断使能
 */
void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI3)
    {
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

        /* 1) PLLI2S 时钟：I2SxCLK = HSE/PLLM * PLLI2SN / PLLI2SR
         *    = 8MHz/4 * 192 / 2 = 192MHz（HAL 据此算出 16kHz 分频） */
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
        PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
        PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
            return;

        /* 2) 使能 SPI3（I2S3）与 GPIO 时钟 */
        __HAL_RCC_SPI3_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* 3) GPIO 复用（AF6）：PC10=I2S3_CK, PA15=I2S3_WS, PC12=I2S3_SD */
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;

        GPIO_InitStruct.Pin = GPIO_PIN_10;          /* PC10 = I2S3_CK */
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
        GPIO_InitStruct.Pin = GPIO_PIN_12;          /* PC12 = I2S3_SD */
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
        GPIO_InitStruct.Pin = GPIO_PIN_15;          /* PA15 = I2S3_WS */
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* 4) DMA 配置：SPI3_RX = DMA1_Stream0_Channel0，循环模式 */
        __HAL_RCC_DMA1_CLK_ENABLE();
        hdma_spi3_rx.Instance = DMA1_Stream0;
        hdma_spi3_rx.Init.Channel = DMA_CHANNEL_0;
        hdma_spi3_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_spi3_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_spi3_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_spi3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_spi3_rx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_spi3_rx.Init.Mode = DMA_CIRCULAR;
        hdma_spi3_rx.Init.Priority = DMA_PRIORITY_HIGH;
        hdma_spi3_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_spi3_rx) != HAL_OK)
            return;

        __HAL_LINKDMA(hi2s, hdmarx, hdma_spi3_rx);

        /* 5) NVIC：DMA 半传输/传输完成中断 */
        HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI3)
    {
        __HAL_RCC_SPI3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10 | GPIO_PIN_12);
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_15);
        HAL_DMA_DeInit(&hdma_spi3_rx);
        HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
    }
}

#else /* !AUDIO_CAPTURE_ENABLE */

/* 占位实现：未启用采集时直接失败 */
int audio_capture_init(void)
{
    return -RT_ERROR;
}

int audio_capture_start(void)
{
    return -RT_ERROR;
}

int audio_capture_stop(void)
{
    return -RT_ERROR;
}

const int16_t *audio_capture_wait_frame(void)
{
    return RT_NULL;
}

#endif /* AUDIO_CAPTURE_ENABLE */
