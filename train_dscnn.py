"""
train_dscnn.py —— 训练 6 句中文语音指令的 DS-CNN 关键词识别(KWS)模型，并导出 int8 .tflite

================================================================================
依赖安装
    pip install tensorflow numpy

数据目录结构（Speech Commands 风格，每类一个子文件夹，内含 16kHz/16bit 单声道 wav）
    data/
      kai_fengshan/    # 开风扇
        001.wav
        ...
      guan_fengshan/   # 关风扇
      kai_chuang/      # 开窗
      guan_chuang/     # 关窗
      kai_jiashi/      # 加湿开
      guan_jiashi/     # 加湿关
      silence/         # 静音（负类，必须有）
      unknown/         # 未知语音（负类，用于拒识，必须有）

运行方式
    python train_dscnn.py

================================================================================
与 MCU 端对齐的硬性要求（部署精度能否保住的关键）：
1. 采样率、帧长(25ms)、帧移(10ms)、MFCC 维数(40)、FFT 点数(512)、Mel 滤波器
   上下限(20~8000Hz)、窗函数(Hann)、预加重系数(0.97) —— 这些必须和
   你在 STM32 上用 CMSIS-DSP 实现的 MFCC 完全一致。
2. 模型输入 shape 为 (98, 40, 1)，其中 98 = 1 秒音频的帧数。MCU 端也要
   按同样方式分帧、pad/truncate 到固定 98 帧再送入 CNN。
3. Python 端 DCT 用的是 tf.signal 的 DCT-II；CMSIS-DSP 端 DCT 的归一化
   方式可能不同，联调时需核对，必要时在 MCU 端做一次系数校准。
================================================================================
"""

import os
import glob
import random
from collections import defaultdict

import numpy as np
import tensorflow as tf

# =============================================================================
# 1. 全局配置
# =============================================================================

DATA_DIR = "data"          # 数据根目录（每个类别一个子文件夹）
OUTPUT_TFLITE = "kws_dscnn_int8.tflite"
OUTPUT_KERAS = "kws_dscnn.h5"

# 类别列表（顺序即标签索引 0..N-1）。前 8 个是目标指令，后两个是负类。
# 必须与 collect_audio.py 的 CLASS_NAMES、voice_app.h 的 voice_cmd_t 顺序一致！
CLASS_NAMES = [
    "kai_chuang",     # 开窗
    "guan_chuang",    # 关窗
    "kai_fengshan",   # 开风扇
    "guan_fengshan",  # 关风扇
    "zhileng",        # 制冷（AC LED 蓝）
    "zhire",          # 制热（AC LED 红）
    "jiashi",         # 加湿（HUMI LED 紫）
    "chushi",         # 除湿（HUMI LED 青）
    "silence",        # 静音负类
    "unknown",        # 未知语音负类
]
NUM_CLASSES = len(CLASS_NAMES)

# ---- 音频与 MFCC 参数（必须与 MCU 端一致）----
SAMPLE_RATE = 16000       # 采样率 Hz
SAMPLE_LEN  = 16000       # 每段音频统一 pad/truncate 到 1 秒 = 16000 样本
FRAME_MS    = 25          # 帧长 ms
STRIDE_MS   = 10          # 帧移 ms
NUM_MFCC    = 40          # MFCC 维数
FFT_LEN     = 512         # FFT 点数
LOW_HZ      = 20          # Mel 滤波器下限 Hz
HIGH_HZ     = 8000        # Mel 滤波器上限 Hz
PREEMPH     = 0.97        # 预加重系数

_frame_length = int(SAMPLE_RATE * FRAME_MS / 1000)
_frame_step   = int(SAMPLE_RATE * STRIDE_MS / 1000)
# 1 秒音频的帧数（pad_end=False 时 = 1 + (n - frame_length) // frame_step）
NUM_FRAMES = 1 + (SAMPLE_LEN - _frame_length) // _frame_step

# ---- 训练超参 ----
# 小数据量（每类几十条）时：epoch 加多、dropout 调大，靠数据增强弥补样本不足；
# 数据量上来后（每类 >=500 条）可改回 EPOCHS=30、BATCH_SIZE=32、DROPOUT=0.3、关闭增强。
BATCH_SIZE   = 8
EPOCHS       = 80
LEARNING_RATE = 1e-3
TRAIN_SPLIT  = 0.8
VAL_SPLIT    = 0.1   # 测试集占 1 - TRAIN_SPLIT - VAL_SPLIT
DROPOUT      = 0.5   # 小数据防过拟合
AUGMENT      = True  # 小数据必备：训练时随机加噪 + 音量/时间偏移

# =============================================================================
# 2. 数据收集与划分（按类别分层随机划分）
# =============================================================================

def collect_samples():
    """返回 [(wav路径, 类别索引), ...]，按 CLASS_NAMES 顺序。"""
    samples = []
    for idx, cls in enumerate(CLASS_NAMES):
        d = os.path.join(DATA_DIR, cls)
        files = sorted(glob.glob(os.path.join(d, "*.wav")))
        if not files:
            print(f"[WARN] 类别目录为空或不存在: {d}")
            continue
        samples += [(f, idx) for f in files]
        print(f"[INFO] {cls}: {len(files)} 条")
    if not samples:
        raise RuntimeError(f"在 {DATA_DIR} 下未找到任何 wav，请检查目录结构")
    return samples


def split_samples(samples):
    """按类别分层，切分 train/val/test。"""
    by_cls = defaultdict(list)
    for p, lab in samples:
        by_cls[lab].append((p, lab))

    train, val, test = [], [], []
    for lab, items in by_cls.items():
        random.shuffle(items)
        n = len(items)
        n_train = int(n * TRAIN_SPLIT)
        n_val   = int(n * VAL_SPLIT)
        train += items[:n_train]
        val   += items[n_train:n_train + n_val]
        test  += items[n_train + n_val:]
    return train, val, test


# =============================================================================
# 3. MFCC 特征提取（tf.signal 实现，作为 tf.data 管道的一环）
# =============================================================================

def _load_wav(path):
    audio = tf.io.read_file(path)
    wav, _ = tf.audio.decode_wav(audio, desired_channels=1)
    wav = tf.squeeze(wav, axis=-1)                 # [samples]
    n = tf.shape(wav)[0]
    wav = tf.cond(
        n >= SAMPLE_LEN,
        lambda: wav[:SAMPLE_LEN],
        lambda: tf.pad(wav, [[0, SAMPLE_LEN - n]]),
    )
    return wav


def extract_mfcc(waveform):
    """输入 [16000] float，输出 (NUM_FRAMES, NUM_MFCC)。"""
    # 预加重，保持长度不变
    waveform = tf.concat(
        [waveform[:1], waveform[1:] - PREEMPH * waveform[:-1]], axis=0)

    stft = tf.signal.stft(
        waveform,
        frame_length=_frame_length,
        frame_step=_frame_step,
        fft_length=FFT_LEN,
        window_fn=tf.signal.hann_window,
        pad_end=False,
    )
    mag = tf.abs(stft)

    num_bins = FFT_LEN // 2 + 1
    mel_w = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins=NUM_MFCC,
        num_spectrogram_bins=num_bins,
        sample_rate=SAMPLE_RATE,
        lower_edge_hertz=LOW_HZ,
        upper_edge_hertz=HIGH_HZ,
    )
    mel = tf.tensordot(mag, mel_w, 1)
    mel.set_shape(mag.shape[:-1].concatenate(mel_w.shape[-1:]))
    log_mel = tf.math.log(mel + 1e-6)
    mfcc = tf.signal.mfccs_from_log_mel_spectrograms(log_mel)  # [T, NUM_MFCC]
    mfcc.set_shape([NUM_FRAMES, NUM_MFCC])
    return mfcc


def make_dataset(samples, shuffle=False, augment=False):
    paths = tf.constant([p for p, _ in samples], dtype=tf.string)
    labels = tf.constant([lab for _, lab in samples], dtype=tf.int32)
    ds = tf.data.Dataset.from_tensor_slices((paths, labels))

    def _map(p, l):
        wav = _load_wav(p)                         # [16000]
        if augment:
            # 小数据增强：随机增益(0.7~1.3) + 随机时间偏移(±0.1s) + 轻噪声
            gain = tf.random.uniform([], 0.7, 1.3)
            wav = wav * gain
            shift = tf.random.uniform([], -1600, 1600, dtype=tf.int32)
            wav = tf.cond(
                shift >= 0,
                lambda: tf.pad(wav, [[shift, 0]])[:SAMPLE_LEN],
                lambda: tf.concat([wav[-shift:], tf.zeros(-shift)], axis=0),
            )
            wav = wav + tf.random.normal(tf.shape(wav), stddev=0.005)
        feat = extract_mfcc(wav)                   # (98, 40)
        feat = tf.expand_dims(feat, axis=-1)       # (98, 40, 1)
        return feat, l

    ds = ds.map(_map, num_parallel_calls=tf.data.AUTOTUNE)
    if shuffle:
        ds = ds.shuffle(1024)
    ds = ds.batch(BATCH_SIZE).prefetch(tf.data.AUTOTUNE)
    return ds


# =============================================================================
# 4. DS-CNN 模型（depthwise-separable CNN，参考 Google KWS）
# =============================================================================

def build_model():
    inp = tf.keras.Input(shape=(NUM_FRAMES, NUM_MFCC, 1), name="mfcc")

    # 通道数用 32 而不是 64：激活内存减半（STM32F407 主 SRAM 只有 128K，
    # 64 通道时 activations ~141K 放不下），MACC 也减半
    x = tf.keras.layers.Conv2D(32, (3, 3), strides=(2, 1), padding="same")(inp)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)

    # DS 块 1
    x = tf.keras.layers.DepthwiseConv2D((3, 3), padding="same")(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.Conv2D(32, (1, 1), padding="same")(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.AveragePooling2D((2, 2))(x)

    # DS 块 2
    x = tf.keras.layers.DepthwiseConv2D((3, 3), padding="same")(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.Conv2D(32, (1, 1), padding="same")(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.AveragePooling2D((2, 2))(x)

    x = tf.keras.layers.Flatten()(x)
    x = tf.keras.layers.Dropout(DROPOUT)(x)
    out = tf.keras.layers.Dense(NUM_CLASSES, activation="softmax", name="logits")(x)

    model = tf.keras.Model(inp, out)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(LEARNING_RATE),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(),
        metrics=["accuracy"],
    )
    return model


# =============================================================================
# 5. 训练
# =============================================================================

def main():
    random.seed(0)
    tf.random.set_seed(0)
    np.random.seed(0)

    print("=== 收集数据 ===")
    all_samples = collect_samples()
    train_s, val_s, test_s = split_samples(all_samples)
    print(f"train={len(train_s)} val={len(val_s)} test={len(test_s)}")

    train_ds = make_dataset(train_s, shuffle=True, augment=AUGMENT)
    val_ds   = make_dataset(val_s)
    test_ds  = make_dataset(test_s)

    print(f"\n=== 构建模型（输入 {NUM_FRAMES}x{NUM_MFCC}，{NUM_CLASSES} 类）===")
    model = build_model()
    model.summary()

    model.fit(train_ds, validation_data=val_ds, epochs=EPOCHS)

    print("\n=== 测试集评估 ===")
    model.evaluate(test_ds)

    # 保存 keras 权重（备查 / 继续训练用）
    model.save(OUTPUT_KERAS)
    print(f"[INFO] 已保存 keras 模型: {OUTPUT_KERAS}")

    # =========================================================================
    # 6. 后训练 int8 量化 + 导出 .tflite
    # =========================================================================
    def representative_data_gen():
        # 校准数据必须带 batch 维 (1, 98, 40, 1)，否则 tflite 校准器报
        # "input->dims->size != 4"
        for feat, _ in train_ds.unbatch().take(200):
            x = tf.expand_dims(tf.cast(feat, tf.float32), axis=0)
            yield [x]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_data_gen
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    tflite_model = converter.convert()
    with open(OUTPUT_TFLITE, "wb") as f:
        f.write(tflite_model)
    print(f"[INFO] 已导出 int8 模型: {OUTPUT_TFLITE} ({len(tflite_model)} bytes)")

    # 可选：打印模型输入输出信息，便于 Cube.AI 导入时核对
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    for d in interpreter.get_input_details():
        print(f"[INFO] 输入: {d['name']} shape={d['shape']} dtype={d['dtype']}")
    for d in interpreter.get_output_details():
        print(f"[INFO] 输出: {d['name']} shape={d['shape']} dtype={d['dtype']}")


if __name__ == "__main__":
    main()
