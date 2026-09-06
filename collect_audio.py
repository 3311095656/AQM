"""
collect_audio.py —— 训练数据采集脚本

功能：用麦克风录制语音指令，自动切成 1 秒片段，按类别归档到 data/<类别>/ 目录，
      生成 train_dscnn.py 可直接使用的数据集（16kHz / 16bit / 单声道 wav）。

依赖安装：
    pip install sounddevice numpy

用法：
    # 交互式：运行后提示输入类别名，逐条录制
    python collect_audio.py

    # 指定类别 + 数量（例如录 200 条 "开风扇"）
    python collect_audio.py --class kai_fengshan --count 200

类别目录结构与 train_dscnn.py 的 CLASS_NAMES 对应：
    kai_chuang / guan_chuang / kai_fengshan / guan_fengshan /
    zhileng / zhire / jiashi / chushi / silence / unknown

录音建议：
    - 环境尽量安静；每条指令自然说话，声音覆盖整 1 秒窗口；
    - silence（静音）：保持安静不发声，录环境底噪；
    - unknown（未知）：说非目标指令（如"你好""今天"等），用于拒识；
    - 建议多个人、不同距离/语气各录一批，总样本每类 >= 500 条效果更好。
"""

import os
import sys
import time
import argparse
import wave

import numpy as np

try:
    import sounddevice as sd
except ImportError:
    print("缺少 sounddevice，请先安装：pip install sounddevice numpy")
    sys.exit(1)

SAMPLE_RATE = 16000      # 采样率，必须与训练/部署一致
DURATION = 1.0           # 每条 1 秒
SAMPLES = int(SAMPLE_RATE * DURATION)
DATA_DIR = "data"

# 与 train_dscnn.py 的 CLASS_NAMES 保持一致（前 8 类顺序 = voice_cmd_t）
CLASS_NAMES = [
    "kai_chuang",
    "guan_chuang",
    "kai_fengshan",
    "guan_fengshan",
    "zhileng",
    "zhire",
    "jiashi",
    "chushi",
    "silence",
    "unknown",
]


def record_one():
    """录制 1 秒音频，返回 int16 numpy 数组（长度 SAMPLES）。"""
    audio = sd.rec(SAMPLES, samplerate=SAMPLE_RATE, channels=1, dtype="int16")
    sd.wait()
    return audio.flatten()


def save_wav(path, audio):
    """把 int16 数组写入 16kHz/16bit/单声道 wav。"""
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)          # 16 bit = 2 字节
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio.tobytes())


def next_index(class_dir, class_name):
    """返回该类别下下一个可用序号，避免覆盖已有文件。"""
    if not os.path.isdir(class_dir):
        return 0
    prefix = f"{class_name}_"
    nums = []
    for f in os.listdir(class_dir):
        if f.startswith(prefix) and f.endswith(".wav"):
            try:
                nums.append(int(f[len(prefix):-4]))
            except ValueError:
                pass
    return max(nums) + 1 if nums else 0


def record_class(class_name, count):
    class_dir = os.path.join(DATA_DIR, class_name)
    os.makedirs(class_dir, exist_ok=True)
    start = next_index(class_dir, class_name)

    print(f"\n=== 录制类别 [{class_name}]，共 {count} 条，从序号 {start} 开始 ===")
    print("每条：提示音后开始说话，1 秒后自动停止。Ctrl+C 提前结束。\n")

    recorded = 0
    try:
        for i in range(count):
            # 倒计时提示（3-2-1，给足准备时间）
            for t in (3, 2, 1):
                print(f"\r[{i + 1}/{count}] {t} 秒后开始说话...", end="", flush=True)
                time.sleep(0.6)
            print("\r[%d/%d] >>> 请说话！ <<<        " % (i + 1, count), flush=True)

            audio = record_one()

            idx = start + i
            path = os.path.join(class_dir, f"{class_name}_{idx:04d}.wav")
            save_wav(path, audio)
            recorded += 1
            print(f"\r[%d/%d] 已保存 %s    " % (i + 1, count, path), flush=True)

            time.sleep(0.3)   # 两条之间短暂停顿
    except KeyboardInterrupt:
        print("\n\n提前结束。")

    print(f"完成：共录制 {recorded} 条 -> {class_dir}")


def main():
    parser = argparse.ArgumentParser(description="训练数据采集")
    parser.add_argument("--class", dest="cls", default=None,
                        help=f"类别名，可选：{', '.join(CLASS_NAMES)}")
    parser.add_argument("--count", type=int, default=50, help="录制条数（默认 50）")
    parser.add_argument("--list", action="store_true", help="列出可用类别并退出")
    args = parser.parse_args()

    if args.list:
        print("可用类别：")
        for i, c in enumerate(CLASS_NAMES):
            print(f"  {i}  {c}")
        return

    cls = args.cls
    if cls is None:
        # 交互式选择类别
        print("可用类别：")
        for i, c in enumerate(CLASS_NAMES):
            print(f"  {i}  {c}")
        raw = input("请输入类别名（或序号）：").strip()
        if raw.isdigit():
            cls = CLASS_NAMES[int(raw)]
        else:
            cls = raw

    if cls not in CLASS_NAMES:
        print(f"未知类别 [{cls}]，请用 --list 查看可用类别")
        sys.exit(1)

    # 检查默认输入设备
    try:
        sd.check_input_settings(samplerate=SAMPLE_RATE, channels=1)
    except Exception as e:
        print(f"警告：当前麦克风可能不支持 16kHz 采样，错误：{e}")
        print("请检查默认输入设备，或修改 SAMPLE_RATE（需同步改 train_dscnn.py 和 mfcc.h）")

    record_class(cls, args.count)


if __name__ == "__main__":
    main()
