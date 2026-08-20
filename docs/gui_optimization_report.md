# EdgeAudio GUI Optimization Report

## 1. 修改内容

- PC GUI 改为“当前状态 / 语音与命令 / 性能 / 重要事件”四块布局。
- Recent events 改为倒序、最多 10 条，只保留连接、语音起止、稳定声音变化、ASR final 和命令。
- ASR partial 与 ASR final 分开显示，final 文本持续保留。
- 当前声音事件只显示稳定事件；Silence、低置信度和重复帧不进入历史。
- 增加实际音频输入设备显示，并支持 `auto` / 显式设备索引。
- 新增 `pc/audio_device.py`，按 K30S/K03S 名称优先选择可用输入设备。
- K03S 的 Windows DirectShow 设备由 FFmpeg 采集并直接转换为 16 kHz mono PCM16；PyAudio 作为回退；未修改 TCP 数据格式。

## 2. 前后变化

之前右侧列表会持续追加每个 YAMNet Top-5，重点事件很快被淹没；现在只显示重要状态变化，最多保留 10 条。

之前 GUI 会把旧板端缺失 RMS 字段显示成 `SPEECH activity (0%)`，容易误判为没有输入。现在讲话状态显示 `SPEECH activity (65%)` 和进度条，静音显示 `QUIET (0%)`。这是旧板端没有发送 `audio_rms` 时的状态回退，不冒充真实 RMS；板端提供 RMS 时仍显示实际 RMS。

## 3. 事件过滤策略

- `Silence` 不进入历史。
- 声音置信度低于 0.12 不进入历史。
- 同一候选声音至少连续出现 3 帧才更新当前稳定事件。
- 重复稳定声音不重复写入历史。
- ASR 只记录 final，partial 仅更新当前显示。
- 历史采用时间倒序，超过 10 条删除最旧项。

## 4. 输入设备选择逻辑

自动选择顺序为：

1. 名称包含 `K30S` 或 `K03S` 的输入设备；
2. 该设备必须能打开单声道 PCM16 输入；
3. 按 Windows 音频后端优先级选择可用项；
4. 没有匹配设备时回退到系统默认输入。

本机实际选中：`XIBERIA K03S`，index 7，Windows DirectSound。交叉测试发现 FFmpeg DirectShow 能采到有效音频（volumedetect 约 `-24.9 dB`），而 PyAudio/DirectSound 读到重复低幅度缓冲，因此自动发送器优先使用 FFmpeg。

## 5. 测试结果

| 测试 | 结果 | 证据 |
|---|---|---|
| 设备枚举 | PASS | GUI 显示 `XIBERIA K03S (index 7, Windows DirectSound)` |
| 设备采集 | PASS | FFmpeg DirectShow 采集到有效音频，volumedetect 约 -24.9 dB |
| 16 kHz 输出格式 | PASS | sender 日志为 `FFmpeg DirectShow -> 16000 Hz mono signed 16-bit PCM` |
| 真实中文模型 | PASS | 板端返回中文 ASR，GUI 显示 `我想说的是` 等结果 |
| 讲话状态显示 | PASS | GUI 显示 `SPEECH activity (65%)`，进度条有变化 |
| Recent events | PASS | 仅保留重要事件，最多 10 条 |
| 核心 AI 链路 | 未修改 | AudioReceiver、RingBuffer、VAD、YAMNet、ASR、Command Parser、TCP 均未改 |

实时 K03S 复测观察到 `speech_start`、`speech=true`、中文 ASR partial/final，证明之前的静音问题来自 Windows PyAudio/DirectSound 采集路径，而不是板端 VAD 或 ASR。

## 6. 性能影响

没有新增 AI 推理线程，也没有复制三份音频。唯一新增工作是 PC 发送器中的 PCM 重采样；板端 YAMNet/ASR 推理路径和协议保持不变。

## 7. Git diff

本轮涉及：

- `pc/audio_device.py`
- `pc/mic_sender.py`
- `pc/edgeaudio_gui.py`
- `tools/start_edgeaudio.ps1`
- `README.md`
- `docs/demo_script.md`
- `docs/gui_optimization_report.md`

没有修改 EdgeVision 项目，也没有修改 EdgeAudio 的 Linux AI 核心模块。
