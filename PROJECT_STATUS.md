# RK3568 EdgeAudio 项目状态

更新时间：2026-08-20
正式 Demo 打磨分支：`feature/final-demo-polish`
正式基线：`agent/edgeaudio-software-complete`
当前基线 commit：`015c4fe style: fix summary whitespace`

## 当前运行方式

正式运行链路保持为：

```text
Windows headset microphone
  -> 16 kHz / mono / PCM16 TCP
  -> RK3568 Linux C++17 AudioReceiver
  -> AudioRingBuffer
  -> 20 ms Energy VAD / Speech Gate
  -> sherpa-onnx Chinese Zipformer CPU ASR
  -> YAMNet 3 s window / RKNN NPU
  -> newline-delimited JSON result server
  -> Windows Tk GUI
```

VM 验证模式使用真实 sherpa-onnx ONNX CPU ASR，YAMNet 明确显示为 `VM MOCK`；板端正式模式使用 YAMNet RKNN/NPU 和 CPU sherpa-onnx ASR。

## 已完成模块

- C++17 Linux AudioReceiver、生命周期和 TCP 音频接收
- 多消费者 AudioRingBuffer 与 PCM16 归一化
- 20 ms Energy VAD / Speech Gate
- YAMNet 3 s / 1.5 s 窗口、Top-K 和时序稳定化
- RK3568 YAMNet RKNN/NPU 路线
- sherpa-onnx 中文 Streaming Zipformer CPU ASR
- Partial / Final ASR 结果和规则型 Command Parser
- 统一 JSON 结果协议与 PC Tk GUI
- Windows 耳机麦克风采集、断线重连
- RK3568 构建、部署、停止、验证和温度保护脚本
- 固定 WAV、TCP、ASR 和板端验收资料

## 本轮允许的范围

- GUI 信息层次、音频电平、命令反馈和错误提示
- 演示脚本、状态文档、README 展示质量
- 不改变正式 ASR 模型、ASR 接口或 AudioPipeline 结构
- 不合并 `feature/asr-rknn-hybrid`

## 明确保留的实验资产

ASR RKNN/Hybrid 实验保持在独立分支 `feature/asr-rknn-hybrid`，包括转换记录、Runtime 定位报告和最小测试程序；这些资产不进入正式 Demo 运行路径。

## 后续优化边界

正式 Demo 本轮只做稳定性与展示打磨。ASR RKNN、第三方模型替换、新 AI 模型、复杂 NLU 和架构重写不属于本轮范围。

## 本轮验证结果

- GUI：真实结果链路显示验证通过，包含连接状态、RMS 电平、VAD、Top-K、Partial/Final、命令动作和后端延迟。
- ARM64 staging build：PASS；修改后的 `audio_receiver` 在 RK3568 编译通过。
- 30 分钟重连回归：PASS，142 次循环，过程记录见 [docs/stability_test_report.md](docs/stability_test_report.md)。
- 演示步骤：见 [docs/demo_script.md](docs/demo_script.md)。
- 现场耳机麦克风演示、最终板端性能 A/B：仍需明天在真实用户环境完成。
