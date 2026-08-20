# EdgeAudio 两分钟 Demo 流程

目标：用一个短流程展示 ARM Linux、实时音频 TCP、VAD、YAMNet RKNN、中文 ASR、命令解析和 PC GUI。

## 开始前

- RK3568 已部署正式 `/root/edgeaudio` 包。
- PC 已安装 `pc/requirements.txt`。
- Windows `XIBERIA K03S`（K30S）耳机麦克风已接入；启动时会自动按名称优先选择它，并在 GUI 的 `Audio input` 显示实际设备。
- Windows DirectSound 对该设备以 44.1 kHz 原生采集最稳定，`mic_sender.py` 会在 PC 端重采样为协议要求的 16 kHz PCM16，不改变板端输入格式。
- `board` 模式启动脚本会让麦克风连接 `192.168.77.2:5700`，PC GUI 连接结果转发端口 `192.168.77.2:5702`；VM 模式仍使用 `127.0.0.1`。
- `board` 模式还会在 RK3568 的 `DISPLAY=:0` 上启动本地 GTK 监视器；板端结果转发器把正式结果端口 `5701` 广播到 `5702`，PC GUI 和板端监视器同时读取同一份 JSON，并显示板端温度。

## 运行

板端启动：

```powershell
.\tools\start_edgeaudio.ps1 -Mode board -BoardHost 192.168.77.2
```

如果只想启动板端，不自动打开 PC 采集和 GUI：

```powershell
.\tools\start_edgeaudio.ps1 -Mode board -BoardHost 192.168.77.2 -NoGui -NoMic
python .\pc\edgeaudio_gui.py --host 192.168.77.2 --port 5702 --input-device auto
python .\pc\mic_sender.py --host 192.168.77.2 --port 5700 --device auto
```

## 120 秒讲解顺序

### 0--20 秒：架构

指出 PC GUI 和板端本地监视器中的 `CONNECTED`、独立的 `SOUND EVENTS · YAMNET` 区域、`YAMNet: RKNN/NPU`、`ASR: cpu`。说明 PC 只负责麦克风和远程展示，核心 AudioReceiver 与板端监视器都在 RK3568 Linux 上运行。

### 20--45 秒：命令闭环

说：

```text
开始监控
```

GUI 应显示中文 Final ASR，并显示：

```text
START_MONITORING | Monitoring Started
```

随后说：

```text
查看状态
```

GUI 应显示 `QUERY_STATUS | Status Queried`。

此时 `Monitoring` 区域应保持 `ON`。

### 45--80 秒：声音事件

先讲话，让 VAD 显示 `speech_start` 和 RMS 变化；然后敲击桌面或播放短音乐，观察 Stable sound event、Top-5、YAMNet latency 和 `EVENT_START/EVENT_END`。

### 80--105 秒：中文 ASR

说：

```text
现在开始测试端侧语音识别
```

GUI 应先更新 Partial，语音结束后更新 Final，并显示 ASR latency 和 RTF。不要把空文本或模拟文本当作通过。

### 105--120 秒：停止与收尾

说：

```text
停止监控
```

GUI 应显示 `STOP_MONITORING | Monitoring Stopped`。最后强调：YAMNet 使用 NPU，ASR 当前正式版本使用 CPU fallback，两个后端状态均来自真实协议字段。

停止后，YAMNet 声音事件和普通 VAD/ASR 结果不再继续刷新，`Monitoring` 变为 `OFF`；音频连接、板端温度和命令监听仍保持运行。再次说“开始监控”后，`Monitoring` 恢复为 `ON`，声音事件和语音结果恢复输出。

## 故障时的最短检查

```powershell
ssh root@192.168.77.2 "tail -50 /root/edgeaudio/logs/audio_receiver.log"
python .\pc\mic_sender.py --list-devices
python .\pc\wav_sender.py .\models\asr\sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23\test_wavs\0.wav --host 192.168.77.2 --port 5700
```

停止 PC 侧本项目进程：

```powershell
.\tools\stop_edgeaudio.ps1
```
