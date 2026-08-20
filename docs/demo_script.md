# EdgeAudio 两分钟 Demo 流程

目标：用一个短流程展示 ARM Linux、实时音频 TCP、VAD、YAMNet RKNN、中文 ASR、命令解析和 PC GUI。

## 开始前

- RK3568 已部署正式 `/root/edgeaudio` 包。
- PC 已安装 `pc/requirements.txt`。
- Windows 耳机麦克风已接入；如设备编号变化，先运行 `python pc/mic_sender.py --list-devices`。
- `board` 模式启动脚本会让 GUI 和麦克风连接 `192.168.77.2` 的 `5700/5701`；VM 模式使用 `127.0.0.1`。

## 运行

板端启动：

```powershell
.\tools\start_edgeaudio.ps1 -Mode board -BoardHost 192.168.77.2
```

如果只想启动板端，不自动打开 PC 采集和 GUI：

```powershell
.\tools\start_edgeaudio.ps1 -Mode board -BoardHost 192.168.77.2 -NoGui -NoMic
python .\pc\edgeaudio_gui.py --host 192.168.77.2
python .\pc\mic_sender.py --host 192.168.77.2 --port 5700
```

## 120 秒讲解顺序

### 0--20 秒：架构

指出 GUI 的 `CONNECTED`、`YAMNet: RKNN/NPU`、`ASR: cpu`。说明 PC 只负责麦克风和展示，核心 AudioReceiver 在 RK3568 Linux 上运行。

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

