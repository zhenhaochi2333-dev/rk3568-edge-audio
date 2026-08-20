# EdgeAudio Stability Test Report

日期：2026-08-20
分支：`feature/final-demo-polish`
测试对象：final-polish staging build on RK3568

## 结论

30 分钟稳定性回归通过。测试使用真实 16 kHz / mono / PCM16 中文 WAV，反复建立和关闭音频 TCP 连接，同时连接结果 TCP 端口读取真实 ASR 和 YAMNet 结果。共完成 142 个循环，所有循环均返回 2 条最终 ASR 和 2 条 `sound_event`，测试客户端正常退出，板端接收进程在测试期间保持存活。

本测试验证的是板端 staging 二进制和正式 Demo 的 CPU sherpa-onnx ASR + YAMNet RKNN/NPU 路线；没有把 ASR RKNN/Hybrid 实验分支并入正式运行路径。

## 测试配置

| 项目 | 值 |
|---|---|
| Board | RK3568 at `192.168.77.2` |
| Audio port | `15700` |
| Result port | `15701` |
| Audio input | `test_wavs/0.wav`，真实中文录音，16 kHz mono PCM16 |
| YAMNet | RKNN/NPU，`yamnet_3s.rknn` |
| ASR | sherpa-onnx streaming Zipformer Chinese 14M，CPU |
| VAD | C++ 20 ms RMS gate |
| Target duration | 1800 s |
| Reconnect interval | 5 s |

## 结果

- Reconnect cycles：`142`
- 每个循环发送耗时：约 `7.72--7.75 s`
- 每个循环最终 ASR：`2`
- 每个循环声音事件：`2`
- 板端进程：测试期间持续存活，无崩溃、无死锁迹象
- 板端 `audio_receiver` RSS：约 `120184 kB`，采样期间无增长
- thermal guard：已启用；本轮采样温度约 `36.7--41.9 °C`，未触发暂停
- 板端日志错误扫描：`0` 条 `error/segmentation/assert/failed/abort` 匹配
- 测试完成后：仅停止了本次 staging 的精确 PID，未停止正式项目进程

## 覆盖范围

- 音频 TCP 断开后重新连接
- 结果 TCP 客户端反复连接和读取
- AudioReceiver 生命周期与 RingBuffer 持续处理
- VAD、CPU ASR partial/final、YAMNet RKNN 事件输出
- JSON 中的 `audio_rms` 与 VAD `rms` 字段
- 温度保护脚本与进程存活

## 限制

本轮 30 分钟循环使用固定真实 WAV，不等同于 30 分钟物理耳机麦克风输入；Windows GUI 已启动并用真实结果链路做过显示验证，但正式板端的现场演示仍需明天用真实耳机执行 [docs/demo_script.md](demo_script.md)。CPU ASR 在 RK3568 上此前测得 20 ms feed RTF 为 `3.5--7.4`，因此正式 Demo 不宣称板端 CPU ASR 已实时；ASR RKNN/Hybrid 仍作为独立实验资产保留。
