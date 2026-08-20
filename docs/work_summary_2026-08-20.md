# RK3568 EdgeAudio 工作总结

日期：2026-08-20

项目：RK3568 EdgeAudio

仓库：[rk3568-edge-audio](https://github.com/zhenhaochi2333-dev/rk3568-edge-audio)

## 1. 本轮目标

本轮目标是在不依赖开发板持续在线开发的前提下，尽可能完成 EdgeAudio 的软件工程，并在开发板恢复可用后补齐真实 ARM64/NPU 验证。

系统目标为：

```text
Windows 麦克风 / WAV
    -> 16 kHz Mono PCM16 TCP
    -> Linux C++17 Receiver
    -> AudioRingBuffer
       -> VAD / Speech Gate -> 中文 ASR
       -> YAMNet 3 s / 1.5 s Window -> RKNN/NPU
    -> JSON Result Server
    -> Windows PC GUI
```

## 2. 已完成的核心工程

### C++ Linux 核心

- C++17 TCP 音频接收器
- PCM16 分片和奇数字节处理
- 多消费者 `AudioRingBuffer`
- 20 ms Energy VAD / Speech Gate
- VAD Speech Start / Speech End
- YAMNet 3 秒窗口、1.5 秒 Hop
- YAMNet Top-K 结果
- `AudioEventStabilizer`
- `EVENT_START`、`EVENT_CHANGE` 和事件结束处理
- sherpa-onnx C API ASR Engine
- ASR Partial / Final 结果
- 中文规则型 Command Parser
- Sound Event、ASR、Status、VAD 统一 JSON 协议
- Result Server 和断线重连基础逻辑
- CPU/NPU Backend 状态字段
- RTF、延迟和推理耗时统计字段

### PC 侧

- Windows 麦克风采集
- 当前耳机麦克风默认设备配置
- 16 kHz、Mono、PCM16 TCP 发送
- 固定 WAV 发送器
- 真实 sherpa-onnx ASR TCP 服务
- Tkinter PC GUI
- 音频电平、连接状态、Top-K、Stable Event、ASR、Command、Latency、RTF 和历史事件显示
- PC 端断线重连逻辑
- ASR 固定 WAV 回归脚本

### 工具和文档

- RK3568 部署脚本
- VM / Board 一键启动脚本
- PC 进程定向停止脚本
- 板端验收脚本
- ARM64 sherpa-onnx 构建脚本
- 板端温度保护脚本
- 第三方项目和许可证记录
- ASR 选型记录
- RK3568 板端验收记录
- 项目 README 和工程差异说明

## 3. ASR 选型结论

正式 ASR 方案：

**sherpa-onnx Streaming Zipformer Chinese 14M**

精确模型：

`sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23`

主要文件：

- int8 Encoder
- FP32 Decoder
- int8 Joiner
- 中文 `tokens.txt`

来源：

- [sherpa-onnx Repository](https://github.com/k2-fsa/sherpa-onnx)
- [Zipformer Model Documentation](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html)
- [Official Model Release](https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23.tar.bz2)

选型原因：

- 中文识别能力满足项目目标
- 支持 Streaming 状态
- 支持 Partial / Final
- 具备成熟 C/C++ API
- 可在 Windows、Linux、ARM64 使用
- 模型规模适合第一版端侧工程
- sherpa-onnx 提供 RKNN 路线，但具体 Zipformer 图仍需板端转换和算子验证

当前计算划分：

```text
PCM16
  -> CPU VAD / Feature Processing
  -> ASR Encoder / Joiner：CPU 已验证，RKNN Hybrid 待验证
  -> CPU Decoder / Tokenizer / Endpoint
  -> Chinese Text
```

Beam Search、Tokenizer、字符串处理和 Command Parser 没有强行放入 NPU。

## 4. YAMNet 结论

声音事件模型继续使用 YAMNet `yamnet_3s`。

参数：

- 输入采样率：16 kHz
- 单声道
- 输入：`[1, 48000]` float32
- Window：3 秒
- Hop：1.5 秒
- 类别数：521 AudioSet classes

来源：[Rockchip RKNN Model Zoo YAMNet](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yamnet)。

板端已使用真实 YAMNet RKNN/NPU 模型完成推理，不能把 YAMNet 当作低延迟 VAD 使用；VAD、YAMNet 和 ASR 三者职责保持分离。

## 5. PC 真实 ASR 测试

测试工具：

`tools/asr_regression.py`

测试内容：

| 文件 | 结果 | 识别结果 / 说明 |
|---|---|---|
| `test_wavs/0.wav` | PASS | `对我做了介绍那么我想说的是大家如果对我的研究感兴趣` |
| `test_wavs/1.wav` | PASS | `重点呢想谈三个问题首先就是这一轮全球金融动能的表现` |
| `test_wavs/8k.wav` | PASS | 正确拒绝：不是 16 kHz Mono PCM16 |

PC ONNX CPU RTF：约 `0.013--0.016`。

测试使用真实模型推理，不接受 Mock 文本作为 ASR PASS。

## 6. RK3568 板端验证

板端环境：

- Host：`192.168.77.2`
- Architecture：`aarch64`
- Linux Kernel：4.19.232
- sherpa-onnx：1.13.2
- ONNX Runtime：1.16.3
- RKNN Runtime：2.3.2

已完成：

- ARM64 sherpa-onnx C API 编译
- `audio_receiver` 同时链接 YAMNet RKNN 和 sherpa CPU ASR
- 真实 TCP PCM 输入
- RingBuffer 处理
- VAD Speech Start / End
- 真实中文 ASR Partial / Final
- 真实 YAMNet RKNN Top-K
- JSON 结果输出

板端真实 ASR 示例：

```text
我做了介绍
我想说的是
如果对我的研究感兴趣
```

YAMNet RKNN 测量：

- 3 秒窗口推理耗时：`42.60--56.66 ms`
- Top-1 多次为 `Speech`
- 稳定层产生 `EVENT_START`

板端 CPU ASR 测量：

- 20 ms feed RTF：约 `3.5--7.4`
- 功能正确
- 当前不是实时方案
- 需要 ASR RKNN / Hybrid 验证后再决定最终后端

## 7. 正式板端运行包

正式目录：

`/root/edgeaudio`

已部署：

- `audio_receiver`
- `yamnet_3s.rknn`
- YAMNet labels
- Zipformer ASR 模型
- `librknnrt.so`
- `libsherpa-onnx-c-api.so`
- `libsherpa-onnx-cxx-api.so`
- `libonnxruntime.so`
- `thermal_guard.sh`

已验证：

- `ldd` 依赖解析通过
- 正式端口 `5700/5701` 启动通过
- 固定 WAV TCP 验收通过
- VAD、ASR、YAMNet JSON 均有输出
- 验证完成后已停止精确 PID，未留下运行中的 EdgeAudio 进程

## 8. 温度保护

脚本：

`tools/thermal_guard.sh`

默认策略：

```text
温度 >= 78 °C：暂停 EdgeAudio 子进程
温度 <= 68 °C：恢复 EdgeAudio 子进程
轮询周期：5 秒
```

支持环境变量覆盖：

- `EDGEAUDIO_THERMAL_PAUSE_C`
- `EDGEAUDIO_THERMAL_RESUME_C`
- `EDGEAUDIO_THERMAL_POLL_S`
- `EDGEAUDIO_THERMAL_FILE`

验证结果：

- 板端 `bash -n` 通过
- 伪温度 80 °C 时触发暂停
- 伪温度 60 °C 时触发恢复
- 实际编译和推理期间温度低于 58 °C
- 本次真实运行未达到暂停阈值，因此没有强行制造过热

## 9. GitHub 和代码质量

本地分支：

`agent/edgeaudio-software-complete`

主要提交：

- `a828b2c feat: build EdgeAudio software pipeline`
- `9173544 fix: complete board ASR and thermal guard`
- `861022d docs: record formal board package validation`

远程 Draft PR：

[PR #1](https://github.com/zhenhaochi2333-dev/rk3568-edge-audio/pull/1)

最终远程提交：

`34590dc42e42debbab748e969903549557029a13`

检查结果：

- Python compile：PASS
- PowerShell AST parse：PASS
- Git diff check：PASS
- Secret pattern scan：未发现 token 或私钥
- EdgeVision 仓库：未修改
- ASR 模型：已加入 Git 忽略规则
- 现有仓库中唯一超过 10 MB 的文件是历史 YAMNet ONNX，约 16.1 MB

## 10. 当前状态

### 已完成

- C++17 Linux 核心
- AudioRingBuffer
- VAD / Speech Gate
- YAMNet Window / Top-K / Stabilizer
- sherpa-onnx C++ ASR Engine
- PC 真实 ASR
- RK3568 真实 YAMNet RKNN
- RK3568 真实 CPU ASR
- JSON 协议
- Result Fusion
- Command Parser
- PC GUI
- 部署、启动、停止、验收工具
- 温度暂停/恢复保护
- README、ASR 选型、第三方记录和板端验收记录

### 尚未最终确定

1. Zipformer Encoder / Joiner 的 RKNN 转换和算子兼容性。
2. CPU 与 Hybrid/NPU 的 RTF、延迟、CPU 占用、内存 A/B 数据。
3. 真实耳机麦克风到 PC GUI 的现场验收。
4. 最终 ASR Backend 是 CPU、Hybrid 还是 RKNN NPU。

## 11. 明日最短验收清单

1. 运行 `tools/validate_board.ps1`，确认正式目录、共享库、设备和温度。
2. 用真实耳机麦克风运行 `pc/mic_sender.py`，默认设备为当前耳机麦克风；必要时先执行 `--list-devices`。
3. 启动 PC GUI，确认音频电平、YAMNet Top-5、Stable Event、ASR Partial / Final、Command、Latency、RTF。
4. 对 CPU ASR 和可用的 RKNN/Hybrid ASR 分别记录 RTF、延迟、CPU、内存和温度。
5. 根据真实数据选择最终 ASR Backend，并更新 README 的最终状态。

## 12. 项目技术亮点总结

EdgeAudio 当前可以概括为：

1. Windows 实时 PCM 音频通过 TCP 输入 ARM Linux。
2. 一个多消费者 AudioRingBuffer 同时服务 VAD、ASR 和 YAMNet 时间轴。
3. YAMNet 在 RK3568 NPU 上进行声音事件识别。
4. 中文 ASR 采用 CPU Decoder 与未来 NPU Neural Encoder 的异构架构。
5. VAD 按需门控 ASR，避免所有环境声音都进入识别。
6. YAMNet 结果经过时序稳定化，减少窗口抖动。
7. Sound Event、ASR、Command、Performance 统一为 JSON 结果流。
8. PC GUI 实时展示声音事件、中文文本和性能数据。
9. 通过 CPU/NPU 实测决定最终部署后端，而不是依据理论算力猜测。

