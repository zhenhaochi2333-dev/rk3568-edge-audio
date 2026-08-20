# EdgeAudio ASR RKNN / Hybrid 可行性调研

日期：2026-08-20

实验分支：`feature/asr-rknn-hybrid`

状态：研究完成，尚未修改正式 ASR、AudioReceiver、GUI 协议或部署脚本。

## 1. 研究目标

当前正式 ASR 使用 sherpa-onnx Streaming Zipformer Chinese 14M，在 RK3568
上的 CPU ASR 已经能够真实识别中文，但实测 20 ms feed RTF 约为
`3.5--7.4`，不能满足实时要求。

本实验只验证合理的 CPU/NPU 异构路线：

```text
PCM16
  -> CPU VAD / PCM处理 / Feature Extraction
  -> RKNN NPU Encoder（优先实验）
  -> CPU Decoder / Tokenizer / Endpoint
  -> Chinese Text
```

Joiner 是否放入 NPU 由实际转换结果和 A/B 测试决定，不为了展示 NPU
强行改变模型结构。

## 2. 研究结论

### 结论摘要

| 项目 | 结论 |
|---|---|
| RK3568 是否有 sherpa-onnx RKNN 路线 | 有。官方文档列出 RK3568 为已知可工作的 RKNPU 平台 |
| Zipformer 是否存在 RKNN 转换路径 | 有。Rockchip RKNN Model Zoo 提供 Encoder、Decoder、Joiner 分组件转换示例 |
| 当前中文 14M 模型是否已有官方 RKNN 包 | 未发现当前精确模型的官方 RKNN 发布包 |
| 当前模型能否直接保证转换 | 不能保证，必须在匹配的 RKNN Toolkit2 环境中实际 build |
| 首先转换哪个部分 | Encoder；它最可能贡献主要计算量，但也是 cache 输入最多、风险最高的部分 |
| Decoder 是否值得上 NPU | 计算量小、输入为 int64、输出仅 320 维，优先保留 CPU |
| Joiner 是否值得上 NPU | 可以作为第二步实验；需测量 CPU Joiner 是否已经足够快 |
| 当前是否进入正式项目 | 否，先保留 CPU fallback 和独立实验资产 |

### 最终研究判断

这是一个**值得做一次受控转换实验**的方向，但当前证据只能证明
“转换路径存在”，不能证明当前 14M 中文模型在 RK3568 上能够达到
`RTF < 1`。

实验必须以真实 RKNN 转换、真实板端输出和 CPU/NPU A/B 数据为准。

## 3. 调研来源

### sherpa-onnx RKNN 文档

[sherpa-onnx RKNN 总览](https://k2-fsa.github.io/sherpa/onnx/rknn/index.html)
列出了 RK3588、RK3576、RK3568、RK3566 和 RK3562 等已知 RKNPU 平台，
并提供构建、模型和导出入口。

[sherpa-onnx RKNN 预训练模型](https://k2-fsa.github.io/sherpa/onnx/rknn/models.html)
展示了使用 `provider=rknn` 加载 Encoder、Decoder、Joiner `.rknn`
文件的 Streaming Zipformer 路径，同时提醒 RKNN Runtime 版本必须和模型
版本匹配。

该页面展示的预转换 Streaming Zipformer 主要是 `rk3588` 命名的模型包，
不能直接等同于当前 `sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23`
已经有可用 RKNN 权重。

### Rockchip RKNN Model Zoo

[RKNN Model Zoo Zipformer 示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/zipformer)
明确列出以下平台：

```text
RK3562, RK3566, RK3568, RK3576, RK3588, RV1126B
```

示例分别转换：

```text
encoder-epoch-99-avg-1.onnx -> encoder-epoch-99-avg-1.rknn
decoder-epoch-99-avg-1.onnx -> decoder-epoch-99-avg-1.rknn
joiner-epoch-99-avg-1.onnx  -> joiner-epoch-99-avg-1.rknn
```

转换脚本使用：

```python
rknn.config(target_platform=platform)
rknn.load_onnx(model=model_path)
rknn.build(do_quantization=do_quant)
rknn.export_rknn(output_path)
```

参考：[Zipformer convert.py](https://raw.githubusercontent.com/airockchip/rknn_model_zoo/main/examples/zipformer/python/convert.py)。

该示例是重要的可行性证据，但示例模型是中英双语 Zipformer，当前模型是
中文 14M；两者的输入长度、cache 形状和模型导出版本必须分别核对。

### RKNN Toolkit2

[Rockchip RKNN Toolkit2](https://github.com/airockchip/rknn-toolkit2)
负责 PC 端 ONNX 到 RKNN 的转换，RKNN Runtime 在板端负责加载和执行。
官方仓库说明 RKNN Toolkit2 支持 RK3566/RK3568 系列，并支持一定范围的
ONNX opset；实际算子支持仍以当前 Toolkit2 版本的 build 日志为准。

## 4. 当前模型结构检查

本地模型目录：

`models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23`

当前包含 FP32 和 ONNX Runtime int8 版本：

```text
encoder-epoch-99-avg-1.onnx
encoder-epoch-99-avg-1.int8.onnx
decoder-epoch-99-avg-1.onnx
decoder-epoch-99-avg-1.int8.onnx
joiner-epoch-99-avg-1.onnx
joiner-epoch-99-avg-1.int8.onnx
```

ONNX Runtime 检查结果：

### 4.1 Encoder

metadata：

```text
model_type       = zipformer
decode_chunk_len = 32
T                = 39
encoder_dims     = 160,160,160,160,160
attention_dims   = 96,96,96,96,96
num_encoder_layers = 2,3,2,2,3
left_context_len = 64,32,16,8,32
```

主输入：

```text
x: [N, 39, 80] float32
```

状态输入包括：

```text
cached_len_0..4   int64
cached_avg_0..4   float32
cached_key_0..4   float32
cached_val_0..4   float32
cached_val2_0..4  float32
cached_conv1_0..4 float32
cached_conv2_0..4 float32
```

典型 cache 形状：

```text
cached_avg_i:  [layers, N, 160]
cached_key_i:  [layers, context, N, 96]
cached_val_i:  [layers, context, N, 48]
cached_conv_i: [layers, N, 160, 30]
```

输出包含：

```text
encoder_out: [N, dynamic_T, 320] float32
new_cached_len_0..4
new_cached_avg_0..4
new_cached_key_0..4
new_cached_val_0..4
new_cached_val2_0..4
new_cached_conv1_0..4
new_cached_conv2_0..4
```

### 4.2 Decoder

```text
input:  y [N, 2] int64
output: decoder_out [N, 320] float32
```

Decoder 的计算规模相对 Encoder 小，但 `int64` 输入和 Streaming 逐 token
调用方式需要验证 RKNN 输入支持。它不是第一优先级。

### 4.3 Joiner

```text
input 0: encoder_out [N, 320] float32
input 1: decoder_out [N, 320] float32
output:  logit [N, 5537] float32
```

Joiner 输出词表 logits，输出宽度较大，但每个 token 的调用计算量仍需要
和 CPU 端实际耗时比较后再决定是否迁移。

## 5. 可转换性分析

### Encoder：可尝试，但风险最高

有利因素：

- Rockchip Model Zoo 有 Zipformer Encoder 转换示例。
- RK3568 在示例支持平台列表中。
- 当前模型已经导出为独立 Encoder ONNX。
- Encoder 的主数据路径是常规 float tensor。

主要风险：

- 一个 Encoder 有大量 cache 输入和输出。
- 当前 ONNX 使用动态 batch `N` 和动态输出时间维度。
- Streaming cache 的 layout 必须和 RKNN Demo 完全一致。
- RKNN 可能需要静态 batch=1 和固定 cache shape。
- 当前模型 `T=39`，而 Model Zoo 示例代码使用的示例模型是另一套
  `x=[1,103,80]` 及不同的 cache 维度，不能直接复制 shape。
- Attention、LayerNorm、Concat、Slice、Reshape、Transpose、MatMul 等
  组合是否被当前 Toolkit2 版本正确融合，必须查看 build 日志和输出校验。

### Decoder：理论上可转换，工程收益可能较小

Decoder 图较小，输入包含 int64 token ids。即使转换成功，CPU Decoder
可能已经足够快，NPU 调用和同步开销可能抵消收益。

### Joiner：可作为第二阶段

Joiner 是独立的两输入一输出图，接口清晰，适合单独转换和对比。但它
每个 token 都可能被调用，是否适合 NPU 取决于调用批量、NPU context
切换和数据拷贝成本。

### 不迁移的部分

以下部分继续放 CPU：

- PCM 处理
- VAD
- 80 维特征提取
- Streaming 状态管理
- Endpoint 判断
- Decoder 控制流
- Tokenizer
- Beam Search 或 Greedy Search
- 中文文本后处理
- Command Parser

## 6. 转换实验路线

实验顺序必须固定，避免无依据地改图。

### Phase A：FP16 / 不量化转换

首先使用 FP32 ONNX，关闭量化：

```text
encoder-epoch-99-avg-1.onnx -> target_platform=rk3568 -> fp RKNN
decoder-epoch-99-avg-1.onnx -> target_platform=rk3568 -> fp RKNN
joiner-epoch-99-avg-1.onnx  -> target_platform=rk3568 -> fp RKNN
```

原因：先区分“算子/shape 不支持”和“量化导致精度问题”。不能一开始
同时引入静态 shape、算子替换和 INT8 量化。

### Phase B：模型级校验

固定相同的 feature 和 cache 输入，比较 ONNX 与 RKNN：

- Encoder output 最大绝对误差
- Encoder output 平均绝对误差
- 每一组 cache output 的误差
- Decoder output 误差
- Joiner logits top-1 / top-5 一致性

如果模型级输出已经明显不一致，不进入 C++ 集成。

### Phase C：INT8 量化

只有 FP16/FP32 RKNN 输出正确后，才使用固定中文测试音频的特征作为
代表性校准数据进行 INT8 转换。

量化后必须重复：

- 模型级数值校验
- 3--5 句中文音频识别
- CPU 与 RKNN 文本对比

### Phase D：C++ Hybrid Engine

仅在模型级转换通过后，新增实验实现：

```text
CpuAsrEngine
RknnHybridAsrEngine
```

上层仍只依赖抽象 `AsrEngine` 接口，不修改：

- `AudioReceiver`
- `AudioRingBuffer`
- JSON 协议
- PC GUI
- YAMNet 流程
- 正式部署脚本

实验实现应至少支持：

- Encoder `.rknn` 路径
- 可选 Joiner `.rknn` 路径
- CPU Decoder fallback
- cache 初始化和更新
- provider/backend 状态
- 每次 Encoder、Joiner 和总 ASR latency
- 出错时明确回退或终止，不伪造 NPU 结果

## 7. A/B 测试设计

### CPU Baseline

当前正式 CPU Engine 作为基线，记录：

- 总 ASR latency
- 20 ms feed latency
- P50/P95 latency
- RTF
- CPU 使用率
- RSS / 峰值内存
- 芯片温度
- 中文文本结果

已知板端基线：

```text
CPU ASR RTF approximately 3.5--7.4
```

### RKNN Hybrid

记录：

- Encoder latency
- Joiner latency
- CPU Decoder latency
- Feature extraction latency
- Cache update latency
- Total latency
- P50/P95 latency
- RTF
- CPU 使用率
- NPU 利用率
- RSS / 峰值内存
- 芯片温度
- 中文文本结果

RKNN 利用率优先读取板端 debugfs，例如：

```bash
watch -n 0.5 cat /sys/kernel/debug/rknpu/load
```

如果设备没有该路径，必须记录为 unavailable，不能把理论 NPU 算力当作
实测利用率。

### 测试集

第一阶段复用当前模型包中的官方中文测试 WAV。正式比较至少扩展到
3--5 个中文句子，并保存：

```text
Expected
CPU result
Hybrid result
CER 或可解释的字符差异
```

速度测试不能替代识别准确率测试。

## 8. 预期收益判断

不预先填写虚假的 NPU 加速倍数。

可能获得收益的条件：

- Encoder 占总推理时间的主要部分。
- RKNN 能够保持 Encoder cache 在可接受的数据布局下运行。
- NPU 调用和 CPU/NPU 数据拷贝开销较小。
- RKNN 输出与 ONNX 输出足够接近。
- CPU Decoder 不成为新的主要瓶颈。

可能没有收益的情况：

- Encoder 每个 chunk 的 cache 拷贝成本很高。
- RKNN 只支持静态形状，需要额外 padding 或重新分配。
- Decoder/Joiner 调用频繁，NPU context 开销过大。
- NPU 算子落到 CPU，导致实际没有完整加速。
- 量化后中文识别错误明显增加。
- 温度升高导致降频，长期 RTF 反而变差。

只有当 Hybrid 同时满足以下条件，才建议进入正式项目：

```text
RTF < 1
CPU 占用明显低于 CPU baseline
中文识别没有明显下降
温度和内存可接受
```

否则保留当前 CPU fallback，不为了使用 NPU 牺牲工程效果。

## 9. 当前实验停止条件

为了控制时间和风险，最多进行 2--3 次有依据的转换尝试：

1. FP16/FP32、原始 ONNX、固定目标 `rk3568`。
2. 根据明确的 build 错误，做一次输入 shape 或 Toolkit2 配置修正。
3. 如果仍因动态 cache、算子或 Runtime 版本阻塞，保留日志并停止继续改图。

禁止：

- 修改正式 `main`
- 删除 CPU ASR
- 覆盖正式部署包
- 修改 AudioReceiver 的正式协议
- 修改 YAMNet 流程
- 为了转换成功大范围重写模型
- 只凭 NPU 理论算力宣称加速

## 10. 下一步实验顺序

1. 在隔离的 RKNN Toolkit2 环境确认版本、目标平台和 Runtime 版本。
2. 对当前 FP32 Encoder 执行一次 `rk3568`、不量化转换。
3. 保存完整转换日志、输入 shape、输出 shape 和生成文件大小。
4. 如果 Encoder 成功，先做固定 feature/cache 的 ONNX 与 RKNN 数值校验。
5. 再依次尝试 Decoder 和 Joiner，不默认全部迁移。
6. 仅模型级校验通过后实现 `RknnHybridAsrEngine`。
7. 使用 3--5 句中文测试集做 CPU/Hybrid 文本和性能 A/B。
8. 将最终数据写入 `docs/asr_backend_comparison.md`。

## 11. 当前结论

```text
ASR RKNN research: FEASIBLE PATH, NOT YET VERIFIED FOR CURRENT MODEL
Current formal backend: CPU sherpa-onnx
First experiment: Encoder-only RKNN with CPU Decoder
CPU fallback: MUST KEEP
Formal project modification: NONE
```

