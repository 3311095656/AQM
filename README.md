# AQM 智慧环境监测系统

## 版本特性

- 基于STM32F407与RT-Thread的多任务架构
- 室内外温湿度、光照、人体存在和燃气阈值检测
- LCD与OLED双屏本地显示
- 风扇、舵机窗户和蜂鸣器联动控制
- WS2812模拟空调、加湿和除湿状态
- 燃气安全优先级控制
- 自动、手动、远程和定时控制模式
- OneNET MQTT物模型属性上报与控制
- 断网期间数据落盘板载 SPI Flash，重连后自动补传
- Node.js代理OneNET HTTPS API
- Web实时监控、远程控制和历史数据展示
- Wi-Fi与MQTT断线重连
- 传感器故障保护与自动恢复

## 端侧语音识别（v2.1）

- 全离线关键词识别（KWS），不依赖云端和网络
- DS-CNN 深度可分离卷积神经网络，经 Cube.AI 部署到 MCU 端侧推理
- 8 条语音指令控制设备：
  - 开窗 / 关窗（舵机窗户）
  - 开风扇 / 关风扇
  - 制冷 / 制热（WS2812 模拟空调）
  - 加湿 / 除湿
- 支持 silence / unknown 拒识，避免误触发
- 音频链路：INMP441 I2S 麦克风 → SPI3+I2S DMA 双缓冲采集（16kHz/16bit 单声道）→ 采集线程环形缓冲 → 推理线程
- 特征提取：MFCC（CMSIS-DSP 库硬件加速）
- 指令枚举与训练类别顺序严格对齐（`voice_cmd_t` ↔ `train_dscnn.py` 的 `CLASS_NAMES`）
- 语音识别模块可整体裁剪：硬件/模型未就绪时安全返回，不启动线程
- 附带完整训练工具链：
  - `collect_audio.py` 音频采集脚本
  - `train_dscnn.py` DS-CNN 训练脚本（输出 `kws_dscnn.h5` / `kws_dscnn_int8.tflite`）
  - `data/` 10 类训练数据集（8 指令 + silence + unknown）
  - `generate_*.py` Cube.AI 模型代码生成脚本

## 修复与优化

- 修复空调自动控制误用室外温度的问题
- 修复普通传感器初始化失败导致系统退出的问题
- 调整MQ-5安全线程启动顺序，保证安全功能优先运行
- 增加传感器失效时的自动控制保护
- 增加传感器每30秒自动重新初始化机制
- 修复定时时段 `enabled` 配置不生效的问题
- 修复定时计划禁用或退出后模式无法恢复的问题
- 增加定时计划进入前状态保存和退出恢复
- 修复MQTT接收缓冲区潜在越界写问题
- 增加MQTT消息长度限制，超长消息返回413
- 增加非法JSON、无效参数对象和数字类型校验
- 修复窗户嵌套布尔值解析错误
- 增加燃气报警期间远程命令安全仲裁
- 燃气报警期间拒绝关闭风扇、窗户和蜂鸣器
- 燃气报警期间拒绝开启空调、湿控和自动模式
- 为非法模式和不安全命令返回明确错误码
- MQTT属性上报和控制回复由QoS0升级为QoS1
- 移除固件与Node同时使用相同MQTT Client ID的冲突架构
- 调整为"固件直连OneNET、Node代理HTTPS API"的端云架构
- 将OneNET Token从浏览器端迁移至Node服务端
- 过滤设备详情接口中的敏感字段
- 增加网页控制结果检查和错误提示
- 修复服务器托管旧版网页的问题
- 统一正式网页与Node API代理路径
- 修复蜂鸣器引脚跨模块访问导致的编译问题
- 完成最新固件重新构建

## 断网数据缓存与补传（v2.2）

利用板载 W25Q64（8MB，SPI2，CS=PB12）实现 MQTT 断网期间的数据缓存，重连后自动补传，解决网络抖动导致的数据丢失：

- 存储后端：RT-Thread SFUD 组件直接访问 W25Q64（`BSP_USING_SPI_FLASH`），不依赖文件系统，开销最小
- 缓存区：Flash 末尾 2MB（512 个 4KB 扇区），每条记录独占一个扇区，容量约 500 条 × 4KB
- 断电安全写入协议：擦扇区 → 写记录头（magic=`AQLG`）+ payload → 回写 magic=`AQOK` 提交；利用 Flash 只能 1→0 翻转的特性免二次擦除，任意时刻掉电均可识别半写入记录
- 数据完整性：记录头 32 字节（magic/seq/timestamp/len/CRC16），payload CRC16-CCITT 校验；坏记录就地作废（写 0 魔数），防止读取游标死循环
- 擦写均衡：512 扇区顺序环形轮转写入，启动时全表扫描 max seq 恢复写入游标
- 自动补传：Paho MQTT 上线回调释放信号量 → 专用补传线程按 seq 从旧到新逐条重发并提交删除，限速避免挤占实时数据带宽
- 断网触发路径：`publish_sensor` 内部检测连接状态，断网时数据自动落盘；网络线程在 MQTT 断线期间继续周期性采集构建数据
- msh 调试命令：`ocache_status`（查看用量/游标）、`ocache_test`（写入测试记录）、`ocache_clear`（清空缓存区）

## 构建修复（v2.1 语音功能引入）

- 新增 `RT_USING_I2S` 独立编译开关：仅构建 HAL I2S 驱动（供 INMP441 采集），不引入 RT-Thread audio 框架组件；SAI 驱动仍由 `RT_USING_AUDIO` 门控
- 自定义链接脚本新增 `.ccm_bss (NOLOAD)` 段：将纯 CPU 访问的大静态缓冲（`rw007_spi`，12.6KB，SPI2 无 DMA）搬迁至 CCM（RAM2 @0x10000000，64K，DMA 不可达），解除 RAM1 溢出，恢复堆空间约 10.8KB
- 修复 NTP 模块 socket 函数隐式声明（补充 `sys/socket.h`）
- 修复 LCD 初始化导出函数签名不匹配：以 `int (*)(void)` wrapper 适配 `INIT_COMPONENT_EXPORT`，消除编译警告及运行时脏参数隐患
- 修复调度模块 `sscanf` 隐式声明（补充 `stdio.h`）
- 清理全部编译警告，固件零警告构建通过

## 构建

- 开发环境：RT-Thread Studio + SCons + Python 3.13 + GNU ARM Embedded 5.4.1
- 硬件平台：STM32F407-RT-SPARK（STM32F407ZGTx）
- 注意：`rtconfig.h` 与 `SConscript` 会被 SCons 按 GBK 编码读取，注释必须使用 ASCII 英文
- 编译产物：`rtthread.bin`（约 511KB，1MB Flash 占用约 50%）
