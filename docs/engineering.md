# EdgeAudio Engineering Notes

本文只记录当前实现中适合面试讲解的工程决策，不扩展项目功能边界。

## 1. TCP 不能按 `recv()` 边界解释 PCM

TCP 是连续字节流。一次 `recv()` 可能返回奇数个字节，也可能把多次发送合并到一起。EdgeAudio 使用 `Pcm16StreamAssembler` 保存最多一个残余 byte，只有凑齐 little-endian 的两个 byte 才生成一个 `int16_t` sample。

客户端断开时残余 byte 会被丢弃，并在下一个客户端连接时重新开始，避免两个连接之间发生字节串接。

协议音频固定为 16-bit signed、little-endian、mono、16 kHz；因此网络层只负责 byte framing，不猜测声道或采样率。

## 2. 为什么使用 3 秒窗口和 1.5 秒 hop

YAMNet 的正式输入为 16 kHz、48000 samples，即 3 秒音频。1.5 秒 hop 产生 50% overlap，使相邻窗口既保留上下文，又不会等待完整 3 秒后才再次检测。`AudioStreamBuffer` 用绝对 sample 时间轴读取窗口，底层存储为有界环形数组，避免持续 `pop_front()` 搬移数据。

## 3. 为什么网络发送裸 PCM

PC 到板端传输的是固定格式的 16 kHz、mono、PCM16 little-endian，不发送 WAV container。这样板端只处理实时音频字节和 framing，不需要在实时链路中解析文件头；WAV 只用于 PC 端确定性测试工具。

## 4. YAMNet RKNN 边界

`YamnetRknnModel` 只负责模型文件、RKNN context、tensor metadata、输入、运行和 output release。启动时查询输入/输出数量和 tensor attributes，输入必须匹配 `[1,48000]` FP32，输出必须找到形状为 `[6,521]` 或 `[1,6,521]` 的 score tensor；代码不会无条件假设 `outputs[2]`。

`YamnetPostProcessor` 再负责 6 帧平均、Top-K 和 AudioSet label 映射。RKNN output 使用 RAII guard 释放，模型析构时销毁 context。

## 5. 资源生命周期

- `TcpAudioReceiver` 析构或 `stop()` 时关闭 server fd；每个 client 的 fd 在读取结束后关闭。
- `ResultPublisher` 负责结果 server、单个下游连接和完整 send loop。
- `YamnetRknnModel` 负责 RKNN context；每次成功 `rknn_outputs_get()` 都由局部 RAII guard 配对 `rknn_outputs_release()`。
- `SIGINT` / `SIGTERM` 设置停止标志，主循环退出后按 receiver、publisher 和模型的 RAII 生命周期收尾。

性能统计只记录真实窗口推理：累计窗口数、窗口间隔、inference average/min/max。今天的离线重构不会产生新的 RK3568 性能数字。

## 6. 测试边界

`tests/edgeaudio_tests.cpp` 不依赖 RKNN 或 sherpa-onnx，覆盖：

- PCM16 2-byte、奇数 byte、随机拆包和连接残余清理；
- 48000 sample 窗口、24000 sample hop、50% overlap 和环形覆盖；
- Top-K 排序、并列分数、空输入、超大 K 和 label 映射。

板端 RKNN 性能数据与本地 unit test 分开记录。无法连接板端时，不能把本地编译或 unit test 写成新的真机验证。
