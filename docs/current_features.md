# RK3568 EdgeAudio 现有功能

> 本文记录当前代码中已经实现或已经验证的功能，不代表未来规划。

## 1. 项目定位

RK3568 EdgeAudio 是一个面向 ARM Linux / 端侧 AI 的实时音频工程，完整链路为：

```text
PC 麦克风 / WAV
    ↓ 16 kHz · Mono · PCM16
TCP 音频输入 :5700
    ↓
RK3568 C++17 AudioReceiver
    ↓
AudioRingBuffer
    ├── VAD / Speech Gate → 中文 ASR
    └── YAMNet 3 秒窗口 → 声音事件识别
    ↓
统一 JSON 结果
    ↓
PC GUI + RK3568 板端 GUI
```

项目同时展示：

- 环境声音是什么；
- 当前是否有人声；
- 用户说了什么中文；
- 语音是否触发了系统命令；
- 当前推理后端和延迟。

## 2. PC 音频输入

- 枚举 Windows 输入设备并显示实际使用设备；
- 自动优先选择名称包含 `K30S` 或 `K03S` 的麦克风；
- 当前实测设备：`XIBERIA K03S`；
- Windows 下优先使用 FFmpeg DirectShow 采集；
- PyAudio 作为回退采集方式；
- 输出固定为 16 kHz、单声道、PCM16 little-endian；
- 支持实时麦克风发送和 WAV 文件发送；
- 支持 TCP 断线后的重新连接。

## 3. Linux / RK3568 核心

正式核心使用 C++17 实现，包括：

- TCP 音频接收；
- 有界 AudioRingBuffer；
- 连续音频时间轴；
- VAD / Speech Gate 调度；
- YAMNet 窗口管理；
- ASR 音频流输入；
- 声音事件时序稳定化；
- ASR、声音事件、命令和性能数据统一输出；
- 生命周期和错误处理。

音频端口：`5700`。

## 4. VAD / Speech Gate

- 使用 20 ms 音频帧进行人声检测；
- 通过 RMS 能量判断 Speech / Silence；
- 产生 `speech_start` 和 `speech_end`；
- 只在人声阶段驱动 ASR，减少无效推理；
- VAD 独立于 YAMNet，YAMNet 不承担低延迟 VAD 职责；
- GUI 显示 VAD 状态和实时音频活动等级。

## 5. YAMNet 声音事件识别

- 模型：YAMNet `yamnet_3s`；
- 输入：`[1, 48000]` float32；
- 采样率：16 kHz；
- 窗口：3 秒；
- 步长：1.5 秒；
- 输出：AudioSet 521 类声音事件 Top-K；
- RK3568 正式后端：RKNN / NPU；
- VM 环境：使用当前配置的 VM backend（正式 C++ VM 默认标记为 `VM MOCK`）；
- GUI 只展示稳定声音事件，不把每个 YAMNet 帧直接刷入历史。

当前声音事件显示策略：

- `Silence` 不作为重要事件显示；
- 置信度低于 `0.12` 的候选被忽略；
- 同一个候选连续出现至少 3 帧后才成为稳定事件；
- 相同稳定事件不会重复写入历史；
- 当前稳定事件和 Top-K 结果分开显示。

已通过真实测试音频验证过 `Whistling` 事件从普通候选变为稳定事件。

## 6. 中文 ASR

- Runtime：sherpa-onnx C API；
- 模型：Streaming Zipformer Chinese 14M；
- 输入：16 kHz PCM；
- 正式 Demo 后端：ARM CPU；
- 支持 partial transcript；
- 支持 final transcript；
- 支持中文语音驱动规则命令；
- 不虚构 ASR confidence；
- 支持固定 WAV 和实时麦克风测试。

已验证：

- Windows 固定中文 WAV 真实模型推理通过；
- RK3568 C++ 核心真实 CPU ASR 通过；
- K03S 实时麦克风能够产生 `speech_start`、中文 partial 和 final 结果。

ASR RKNN / Hybrid 仍是隔离实验资产，不属于当前正式 Demo 默认运行路径。当前正式方案为 CPU fallback，等待真实板端 A/B 性能决定是否合入 Hybrid。

## 7. Command Parser

ASR final 文本可以进入简单规则解析器，当前用于展示语音驱动系统逻辑，例如：

| 语音内容 | 命令 |
|---|---|
| 开始监控 | `START_MONITORING` |
| 停止监控 | `STOP_MONITORING` |
| 系统状态 / 查看状态 | `QUERY_STATUS` |

命令现在会真正改变监控状态：

- `START_MONITORING`：开启 YAMNet、VAD/ASR 结果和正常监控输出；
- `STOP_MONITORING`：停止 YAMNet 和普通 VAD/ASR 结果输出；
- 停止状态仍保留轻量命令监听，因此仍可以说“开始监控”恢复监控；
- 停止状态仍保留音频连接、状态 JSON 和温度保护；
- `QUERY_STATUS`：查询当前运行状态，不改变监控开关。

PC GUI 和板端 GUI 都会在独立的 Monitoring 区域显示 `ON` / `OFF`。

当前没有引入 LLM、复杂 NLU 或独立 KWS 模型。

## 8. 统一结果协议

结果通过 newline-delimited JSON 发送。

支持的主要结果类型：

- `status`：连接、VAD、音频 RMS、后端和性能状态；
- `vad`：语音开始或结束；
- `sound_event`：YAMNet Top-K、稳定事件、推理耗时；
- `asr`：partial / final 中文文本、延迟和 RTF；
- `command`：解析后的系统命令；
- `performance`：运行性能数据。

正式结果端口：`5701`。

状态消息包含 `monitoring` 字段，用于同步当前监控开关。

## 9. PC GUI

PC GUI 使用 Tkinter，当前按功能分区展示：

### SOUND EVENTS · YAMNET

- Monitoring 状态；
- 当前稳定声音事件；
- YAMNet Top-5；
- Sound backend。

### SPEECH & COMMAND

- 连接状态和输入设备；
- Mic / VAD 和音频等级；
- ASR partial；
- ASR final；
- Command；
- ASR backend。

### PERFORMANCE / IMPORTANT EVENTS

- YAMNet latency；
- ASR latency；
- ASR RTF；
- 最近 10 条重要事件。

PC GUI 具体展示内容包括：

- RK3568 / VM 连接状态；
- 实际音频输入设备；
- Mic / VAD 状态；
- 音频 RMS 和活动进度条；
- 当前稳定声音事件；
- YAMNet Top-K；
- ASR partial；
- ASR final；
- 识别出的 Command；
- YAMNet backend；
- ASR backend；
- YAMNet latency；
- ASR latency；
- ASR RTF；
- 最近 10 条重要事件。

历史事件只记录连接状态、Speech Start/End、稳定声音变化、ASR final 和命令，避免静音及重复 YAMNet 输出淹没重点。

## 10. RK3568 板端 GUI

板端使用 GTK3 / PyGObject 在已有 Xorg 显示器上运行，属于显示层，不运行第二套 AI pipeline。

板端 GUI 同样按 `CURRENT STATUS`、`SOUND EVENTS · YAMNET`、`SPEECH & COMMAND`、`PERFORMANCE` 和重要事件区域展示：

- 独立的 YAMNet 声音事件区域；
- 独立的 ASR 与命令区域；
- 独立的板端状态和温度区域。

板端 GUI 具体展示：

- 核心连接状态；
- 输入流状态；
- VAD 和音频活动条；
- 稳定声音事件；
- YAMNet backend 和延迟；
- ASR partial / final；
- Command；
- ASR backend、延迟和 RTF；
- 板端 SoC 温度；
- 最近 10 条重要事件。

板端结果中继从核心 `5701` 读取原始 JSON，再广播到 `5702`，使 PC GUI 和板端 GUI 可以同时查看同一份结果，不增加推理线程。

## 11. 稳定性和温度保护

- 板端使用 thermal guard 监控 `/sys/class/thermal/thermal_zone*/temp`；
- 温度达到约 `78°C` 时暂停 EdgeAudio 进程；
- 降温到约 `68°C` 后恢复；
- 连接客户端使用阻塞读取，避免静音期间因读取超时产生假断线重连；
- PC GUI、板端 GUI 和结果中继均支持断线重连；
- 停止脚本只停止 EdgeAudio 相关 PID，不批量结束系统 Python、SSH 或 FFmpeg。

## 12. 部署与运行工具

| 工具 | 功能 |
|---|---|
| `tools/deploy_rk3568.ps1` | 构建和部署板端二进制、模型、脚本及共享库 |
| `tools/start_edgeaudio.ps1` | 启动 VM 或 RK3568 Board 模式 |
| `tools/stop_edgeaudio.ps1` | 停止 EdgeAudio 相关进程 |
| `tools/validate_board.ps1` | 检查 SSH、架构、文件、模型、端口和运行状态 |
| `tools/thermal_guard.sh` | 板端温度监控和暂停恢复 |
| `pc/wav_sender.py` | 发送固定 16 kHz WAV 做确定性测试 |
| `pc/mic_sender.py` | 采集实时麦克风并发送 PCM TCP 流 |

## 13. 当前真实验证状态

| 功能 | 状态 |
|---|---|
| K03S 麦克风选择 | 已验证 |
| K03S 实时 PCM 采集 | 已验证 |
| PC → RK3568 TCP 音频 | 已验证 |
| C++ AudioReceiver | 已验证 |
| AudioRingBuffer | 已验证 |
| VAD Speech Start/End | 已验证 |
| 中文 ASR partial/final | 已验证 |
| Command Parser | 已验证 |
| YAMNet RKNN/NPU | 板端已验证 |
| YAMNet 稳定事件显示 | 已验证 |
| PC GUI | 已验证 |
| 板端 GUI | 已验证 |
| 双端同时查看结果 | 已验证 |
| 静音期间稳定运行 | 已验证 |
| 温度监控 | 已启用 |
| ASR RKNN/Hybrid | 独立实验，未作为正式 Demo 默认后端 |

## 14. 当前明确不包含

当前版本不包含：

- Speaker Recognition / 声纹识别；
- TTS；
- LLM 或 Agent；
- 独立 KWS 神经模型；
- 声源定位和麦克风阵列；
- Web UI；
- 数据库；
- 在线训练或微调。

这些内容不属于当前 EdgeAudio 第一版的功能边界。
