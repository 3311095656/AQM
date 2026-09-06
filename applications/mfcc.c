/*
 * mfcc.c —— 与 TensorFlow 端严格对齐的 MFCC 特征提取
 *
 * 对齐目标：train_dscnn.py 中的
 *   tf.signal.stft + tf.signal.linear_to_mel_weight_matrix
 *   + tf.math.log + tf.signal.mfccs_from_log_mel_spectrograms
 *
 * 已对齐的关键细节：
 *   1. 预加重  y[n] = x[n] - 0.97*x[n-1]（y[0]=x[0]，用原始 x[n-1]）
 *   2. 分帧    400 样本 / 步进 160，共 98 帧
 *   3. Hann 窗（周期）：0.5 - 0.5*cos(2*pi*n/400)
 *   4. FFT     512 点，未归一化，取前 257 bin 的幅度 |X|
 *   5. Mel 滤波器组：HTK 公式 mel=1127*ln(1+f/700)，DC bin 置零，
 *      三角插值在 mel 域（非 Hz 域）
 *   6. log     log(mel + 1e-6)
 *   7. DCT-II  未归一化 DCT-II 后统一乘 sqrt(2/N)（HTK 约定）
 *
 * FFT 两种实现（由 MFCC_USE_CMSIS_DSP 开关切换）：
 *   - 纯 C radix-2 FFT（默认，无依赖）
 *   - CMSIS-DSP arm_rfft_fast_f32（需启用 CMSIS-DSP 库）
 * 两者均为未归一化 FFT，缩放一致，数值等价。
 */

#include <math.h>
#include <string.h>
#include "mfcc.h"

#if MFCC_USE_CMSIS_DSP
#include "arm_math.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

typedef struct {
    float re;
    float im;
} mfcc_cplx_t;

/* ---- 预计算表（mfcc_init 填充，放 RAM） ---- */
static float g_hann[MFCC_FRAME_LEN];                       /* Hann 窗 */
static float g_dct[MFCC_NUM_COEFFS][MFCC_NUM_COEFFS];      /* DCT-II 矩阵 */
static float g_bin_mel[MFCC_FFT_LEN / 2 + 1];              /* FFT bin -> mel */
static float g_mel_edges[MFCC_NUM_COEFFS + 2];             /* mel 域边缘点 */

/* ---- FFT 相关缓冲 ---- */
static float g_fft_in[MFCC_FFT_LEN];   /* 实数输入（加窗后） */
#if MFCC_USE_CMSIS_DSP
static arm_rfft_fast_instance_f32 g_rfft;
static float g_fft_out[MFCC_FFT_LEN];  /* arm_rfft 输出（特殊交错格式） */
#else
static mfcc_cplx_t g_fft[MFCC_FFT_LEN];
#endif

/* ---- 工作缓冲 ---- */
static float g_mag[MFCC_FFT_LEN / 2 + 1];
static float g_mel[MFCC_NUM_COEFFS];

/* HTK mel 频率转换 */
static float hz_to_mel(float hz)
{
    return 1127.0f * logf(1.0f + hz / 700.0f);
}

#if !MFCC_USE_CMSIS_DSP
/* 迭代 radix-2 FFT，输出自然序，未归一化（与 tf 的 FFT 一致） */
static void mfcc_fft(mfcc_cplx_t *x, int n)
{
    int i, j, k, len;

    /* bit-reversal */
    for (i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            mfcc_cplx_t t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }

    /* butterfly */
    for (len = 2; len <= n; len <<= 1)
    {
        float ang = -2.0f * M_PI / (float)len;
        float wl_re = cosf(ang);
        float wl_im = sinf(ang);
        int half = len >> 1;

        for (i = 0; i < n; i += len)
        {
            float wr = 1.0f, wi = 0.0f;
            for (k = 0; k < half; k++)
            {
                mfcc_cplx_t u = x[i + k];
                float vr = x[i + k + half].re * wr - x[i + k + half].im * wi;
                float vi = x[i + k + half].re * wi + x[i + k + half].im * wr;

                x[i + k].re = u.re + vr;
                x[i + k].im = u.im + vi;
                x[i + k + half].re = u.re - vr;
                x[i + k + half].im = u.im - vi;

                float nwr = wr * wl_re - wi * wl_im;
                wi = wr * wl_im + wi * wl_re;
                wr = nwr;
            }
        }
    }
}
#endif /* !MFCC_USE_CMSIS_DSP */

int mfcc_init(void)
{
    int i, k;
    const float nyquist = MFCC_SAMPLE_RATE / 2.0f;
    const int n_bins = MFCC_FFT_LEN / 2 + 1;

    /* 1) Hann 窗（周期） */
    for (i = 0; i < MFCC_FRAME_LEN; i++)
        g_hann[i] = 0.5f - 0.5f * cosf(2.0f * M_PI * (float)i / MFCC_FRAME_LEN);

    /* 2) DCT-II 矩阵，含 HTK 缩放 sqrt(2/N)（对齐 mfccs_from_log_mel） */
    {
        const float s = sqrtf(2.0f / MFCC_NUM_COEFFS);
        for (k = 0; k < MFCC_NUM_COEFFS; k++)
            for (i = 0; i < MFCC_NUM_COEFFS; i++)
                g_dct[k][i] = s * cosf(M_PI * (float)k * (2.0f * (float)i + 1.0f) /
                                       (2.0f * MFCC_NUM_COEFFS));
    }

    /* 3) FFT bin -> mel 映射（bin k 频率 = k * nyquist / (n_bins-1)） */
    for (k = 0; k < n_bins; k++)
        g_bin_mel[k] = hz_to_mel((float)k * nyquist / (float)(n_bins - 1));

    /* 4) mel 域边缘点（num_coeffs+2 个，均匀分布） */
    {
        float mel_low = hz_to_mel(MFCC_LOW_HZ);
        float mel_high = hz_to_mel(MFCC_HIGH_HZ);
        for (i = 0; i < MFCC_NUM_COEFFS + 2; i++)
            g_mel_edges[i] = mel_low +
                             (mel_high - mel_low) * (float)i /
                             (float)(MFCC_NUM_COEFFS + 1);
    }

#if MFCC_USE_CMSIS_DSP
    /* 5) 初始化 CMSIS-DSP 实数 FFT 实例 */
    arm_rfft_fast_init_f32(&g_rfft, MFCC_FFT_LEN);
#endif

    return 0;
}

int mfcc_extract(const int16_t *pcm, float *features)
{
    const int n_bins = MFCC_FFT_LEN / 2 + 1;
    const float inv32768 = 1.0f / 32768.0f;
    int frame, k, m;

    for (frame = 0; frame < MFCC_NUM_FRAMES; frame++)
    {
        int base = frame * MFCC_FRAME_STEP;

        /* 1) 预加重 + 分帧 + Hann 窗 -> FFT 实数输入 */
        for (k = 0; k < MFCC_FFT_LEN; k++)
        {
            float x = 0.0f;
            if (k < MFCC_FRAME_LEN)
            {
                int idx = base + k;
                float cur = (float)pcm[idx] * inv32768;
                float pre;

                if (idx == 0)
                    pre = cur;   /* y[0] = x[0] */
                else
                    pre = cur - MFCC_PREEMPH * (float)pcm[idx - 1] * inv32768;

                x = pre * g_hann[k];
            }
            g_fft_in[k] = x;
        }

        /* 2) FFT -> 幅度谱 */
#if MFCC_USE_CMSIS_DSP
        arm_rfft_fast_f32(&g_rfft, g_fft_in, g_fft_out, 0);
        /* arm_rfft 输出交错格式：out[0]=DC, out[1]=Nyquist,
         * out[2k]=Re(bin k), out[2k+1]=Im(bin k) */
        g_mag[0] = fabsf(g_fft_out[0]);
        for (k = 1; k < n_bins - 1; k++)
        {
            float re = g_fft_out[2 * k];
            float im = g_fft_out[2 * k + 1];
            g_mag[k] = sqrtf(re * re + im * im);
        }
        g_mag[n_bins - 1] = fabsf(g_fft_out[1]);
#else
        for (k = 0; k < MFCC_FFT_LEN; k++)
        {
            g_fft[k].re = g_fft_in[k];
            g_fft[k].im = 0.0f;
        }
        mfcc_fft(g_fft, MFCC_FFT_LEN);
        for (k = 0; k < n_bins; k++)
            g_mag[k] = sqrtf(g_fft[k].re * g_fft[k].re +
                             g_fft[k].im * g_fft[k].im);
#endif

        /* 3) Mel 滤波器组（DC bin 置零，mel 域三角插值）-> log */
        for (m = 0; m < MFCC_NUM_COEFFS; m++)
        {
            float lower_mel = g_mel_edges[m];
            float center_mel = g_mel_edges[m + 1];
            float upper_mel = g_mel_edges[m + 2];
            float sum = 0.0f;

            for (k = 1; k < n_bins; k++)   /* k=0（DC）跳过，与 tf 一致 */
            {
                float bin_mel = g_bin_mel[k];
                float lo = (bin_mel - lower_mel) / (center_mel - lower_mel);
                float hi = (upper_mel - bin_mel) / (upper_mel - center_mel);
                float w = lo < hi ? lo : hi;
                if (w < 0.0f)
                    w = 0.0f;
                sum += w * g_mag[k];
            }
            g_mel[m] = logf(sum + 1e-6f);
        }

        /* 4) DCT-II */
        for (k = 0; k < MFCC_NUM_COEFFS; k++)
        {
            float acc = 0.0f;
            for (m = 0; m < MFCC_NUM_COEFFS; m++)
                acc += g_dct[k][m] * g_mel[m];
            features[frame * MFCC_NUM_COEFFS + k] = acc;
        }
    }

    return 0;
}
