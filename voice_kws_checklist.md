# 端侧语音识别（KWS）接入 Checklist

在 STM32F407ZG 上实现 6 句中文指令的端侧识别。本清单覆盖「训练 → 部署 → 硬件接入 → 联调」全流程，逐项勾选即可。

相关代码文件（均在 `applications/` 下）：

| 文件 | 职责 |
|---|---|
| [train_dscnn.py](file:///f:/RT-ThreadStudio/workspace/project_aqm/train_dscnn.py) | 训练 DS-CNN + 导出 int8 tflite |
| [mfcc.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/mfcc.c) / [mfcc.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/mfcc.h) | MFCC 特征提取（与训练端严格对齐） |
| [kws_model.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/kws_model.c) / [kws_model.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/kws_model.h) | Cube.AI 推理封装 |
| [audio_capture.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/audio_capture.c) / [audio_capture.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/audio_capture.h) | I2S + DMA 双缓冲采集 |
| [voice_app.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/voice_app.c) / [voice_app.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/voice_app.h) | 线程 + 后处理 + 指令映射 |

---

## 阶段 0：前置准备

- [ ] 麦克风硬件：I2S 数字麦克风（推荐 INMP441），确认 SD/WS/CK 引脚接法
- [ ] STM32F407 开发板 + ST-Link 调试器
- [ ] 安装 STM32CubeMX（6.x+）并联网安装 X-CUBE-AI 扩展包（支持 STM32F4）
- [ ] PC 端 Python 环境：`pip install tensorflow numpy`

---

## 阶段 1：训练模型（PC 端）

- [ ] 按 [train_dscnn.py](file:///f:/RT-ThreadStudio/workspace/project_aqm/train_dscnn.py) 顶部注释，组织数据目录 `data/`（每类一个子文件夹，16kHz/16bit 单声道 wav）
- [ ] 准备 6 句指令数据，每句 ≥ 500 条（多人、多语气、含背景噪声增强）
- [ ] 准备负类：`silence/`（静音）+ `unknown/`（未知语音），**两者必须存在**，否则无法拒识
- [ ] 修改 `CLASS_NAMES` 为你的实际 6 句指令（顺序 = 标签索引，前 6 类是有效指令）
- [ ] 核对 MFCC 参数与 MCU 端一致：`FRAME_MS=25 / STRIDE_MS=10 / NUM_MFCC=40 / FFT_LEN=512 / LOW_HZ=20 / HIGH_HZ=8000 / PREEMPH=0.97`
- [ ] 运行 `python train_dscnn.py`
- [ ] 检查输出：测试集准确率 ≥ 目标阈值（建议 ≥90%）
- [ ] 得到 `kws_dscnn_int8.tflite`（int8 全量化）

---

## 阶段 2：Cube.AI 部署

- [ ] CubeMX 中 `Software Packs` 启用 X-CUBE-AI
- [ ] X-CUBE-AI 配置面板 `Add network`，导入 `kws_dscnn_int8.tflite`，网络名填 `kws_dscnn`
- [ ] `Analyze`，核对资源预算：
  - [ ] `weights(ROM)` ≤ ~500KB（Flash 余量内）
  - [ ] `activations(RAM)` 在预留内存内（结合 CCM/主 SRAM）
- [ ] `Validation`（desktop）通过，误差在 int8 正常范围
- [ ] `Generate Code`，得到 `kws_dscnn.h` / `kws_dscnn_data.h` 等（若生成到的是临时工程，拷文件即可）
- [ ] 生成代码拷入工程：`kws_dscnn.c/h`、`kws_dscnn_data.c/h` 放到 `applications/`（该目录 SConscript 自动编译 `*.c`，头文件搜索路径已含该目录）
- [ ] 拷贝 AI 运行库：从 X-CUBE-AI 安装包 `Middlewares/ST/ai_runtime/` 复制 `Include/` 与 `Lib/GCC/`（`libai_runtime.a`、`libcmsis_nn.a` 等）到工程，建议放 `libraries/ai_runtime/`
- [ ] 工程属性（RT-Thread Studio → 右键工程 → Properties → C/C++ Build → Settings）：
  - [ ] GCC C Compiler → Include paths：添加运行库 Include 路径（如 `../libraries/ai_runtime/Include`，写法以工程现有 Include paths 风格为准）
  - [ ] GCC C Linker → Libraries：添加 `ai_runtime`（及 `cmsis_nn`，以 `Lib/GCC` 下实际 .a 文件名为准）；Library search path：添加对应 `Lib/GCC` 目录
- [ ] 编译，按报错核对 [kws_model.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/kws_model.c) 文件头【符号对照表】：
  - [ ] 头文件名 `kws_dscnn.h` / `kws_dscnn_data.h`
  - [ ] 函数 `ai_kws_dscnn_create_and_init / inputs_get / outputs_get / run / get_report`（6.x 旧版 `get_report` 叫 `get_info`）
  - [ ] 大小宏 `AI_KWS_DSCNN_DATA_ACTIVATIONS_SIZE`（6.x 旧版叫 `..._ACTIVATION_1_SIZE`）、`AI_KWS_DSCNN_IN_1_SIZE_BYTES / OUT_1_SIZE_BYTES`
- [ ] 核对量化参数：`KWS_QUANT_FROM_REPORT=1` 时若 `quantization.scale[] / zero_point[]` 字段报错，对照运行库 `ai_datatypes_generics.h` 微调；或改为硬编码（数值取自 Analyze 面板 scale / zero-point 列）
- [ ] 将 [kws_model.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/kws_model.h) 中 `KWS_MODEL_ENABLE` 置 `1`

---

## 阶段 3：硬件接入（I2S 采集）

- [x] CubeMX 移除 SDIO（SD 卡）：`Mode` 选 `Disabled`，腾出 PC10/PC12 —— 已完成
- [x] I2S3 底层初始化：已手写在 [audio_capture.c](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/audio_capture.c) 的 `HAL_I2S_MspInit / MspDeInit`（含 PLLI2S 时钟、GPIO 复用 PC10/PA15/PC12、DMA、NVIC），**无需再在 CubeMX 里配置 I2S**
- [ ] 在 [stm32f4xx_hal_conf.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/board/CubeMX_Config/Inc/stm32f4xx_hal_conf.h) 取消注释 `#define HAL_I2S_MODULE_ENABLED`
- [ ] 注释掉 [stm32f4xx_hal_conf.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/board/CubeMX_Config/Inc/stm32f4xx_hal_conf.h) 里的 `#define HAL_SD_MODULE_ENABLED`（移除 SD 卡后）
- [ ] 声道处理：若麦克风左右声道交织输出，在 `HAL_I2S_RxHalfCpltCallback / RxCpltCallback` 中做左声道抽取（见代码内 TODO）
- [ ] 将 [audio_capture.h](file:///f:/RT-ThreadStudio/workspace/project_aqm/applications/audio_capture.h) 中 `AUDIO_CAPTURE_ENABLE` 置 `1`

---

## 阶段 4：联调验证

- [ ] 编译下载，确认无编译错误
- [ ] 启动后串口出现 `voice recognition enabled`（若仍提示 init skipped，说明开关未全部置 1）
- [ ] 运行 `voice_status` 确认 `Capture=1 Model=1`
- [ ] 无麦克风时，用 `voice_trigger <0..5>` 手动验证后处理/指令映射逻辑
- [ ] MFCC 对齐验证（关键）：
  - [ ] 用一段已知音频（正弦波/白噪声），分别跑 `train_dscnn.py` 的 `extract_mfcc()` 和 MCU 端 `mfcc_extract()`
  - [ ] 逐帧对比 MFCC 数值，误差应在量化/浮点精度内
  - [ ] 若偏差大，重点排查：FFT 缩放、Mel 域插值、DCT 归一化、DC bin、Hann 窗
- [ ] 端到端测试：说话 → 识别 → 设备动作正确，无重复触发/误触发
- [ ] 调参（如识别不稳定）：
  - [ ] `VOICE_CONF_THRESHOLD`（置信度阈值）
  - [ ] `VOICE_DEBOUNCE_CNT`（去抖次数）
  - [ ] `VOICE_TRIG_INTERVAL_MS`（触发抑制间隔）

---

## 常见坑

| 现象 | 排查 |
|---|---|
| 部署精度大幅下降 | MFCC 参数不一致（帧长/帧移/维数/FFT/窗/DCT 归一化） |
| 采样率不对 | I2S `AudioFreq` 与 PLLI2S 时钟树未匹配 |
| 音频全是噪声/丢帧 | DMA 缓冲不足、推理线程优先级过低被抢占 |
| 只识别到静音 | 左声道未正确抽取，或 `silence` 负类数据过多 |
| RAM 溢出 | 激活内存过大，需将 `g_activations`/`g_mfcc` 放 CCM 或精简模型 |
