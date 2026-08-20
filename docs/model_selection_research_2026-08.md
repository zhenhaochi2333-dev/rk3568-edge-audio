# RK3568 EdgeAudio 实时声音事件检测模型选型专项调研

> 调研时间：2026 年 8 月
>
> 目标：为当前 RK3568 EdgeAudio 项目选出一个能够快速、稳定形成真实端侧 Demo 的声音事件检测模型。
>
> 本文只完成模型选型，不自动开始代码改造、模型下载或重新转换。

## 1. Executive Decision

### 首选：YAMNet `yamnet_3s`

当前项目继续使用 YAMNet，具体部署形态为：

```text
YAMNet yamnet_3s.onnx
    -> RKNN FP16
    -> RK3568 NPU
```

YAMNet 不是公开指标最高的模型，但它是当前候选中唯一同时具备以下条件的模型：

1. 面向通用声音事件，而不是只做关键词识别。
2. Rockchip 官方 RKNN Model Zoo 提供 YAMNet 示例。
3. 官方 Model Zoo 明确列出 RK3568 支持。
4. 输入参数清晰：16 kHz、单声道 PCM。
5. ONNX 到 RKNN 的转换路径已经存在。
6. 当前项目已经在真实 RK3568 板端完成 NPU 推理。
7. 当前 PC 麦克风、TCP、滑动窗口、Top-K 输出链路已经跑通。

Rockchip 官方 Model Zoo 将 `yamnet_3s.onnx` 列为 RK3568 支持的 Speech Classification 模型，并提供 ONNX 到 RKNN 的转换与推理示例。

来源：

- [Rockchip RKNN Model Zoo](https://github.com/airockchip/rknn_model_zoo)
- [Rockchip YAMNet RKNN 示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yamnet)
- [YAMNet ONNX 导出说明](https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yamnet/export_onnx.md)

### 备选

1. **EfficientAT `mn04_as`**：精度/模型规模很有吸引力，但没有直接 RK3568/RKNN 证据。
2. **CED-Tiny**：公开指标较高并支持 ONNX，但存在 GPL-3.0 与 Transformer/RKNN 风险，只适合作为研究型候选。

---

## 2. Project Constraints

当前项目的真实约束：

- SoC：Rockchip RK3568
- 系统：ARM64 Linux
- 推理：RKNN/NPU
- 正式端：C++17
- PC 端：Windows 麦克风
- 传输：POSIX Socket / TCP
- 当前音频硬件：PC 麦克风、耳机麦克风
- 近期目标：最小实时 Sound Event Detection Demo
- 输入优先：16 kHz、单声道、PCM16
- 未来方向：Ring Buffer、VAD、Temporal Stabilizer、事件起止、INT8 PTQ、真实数据 Fine-tune

当前项目不希望为了一个第一版音频 Demo 直接升级整套稳定 RKNN 环境，也不希望把只能在桌面 GPU 上运行的模型当成板端方案。

项目当前已有实际验证：

- YAMNet ONNX 已转换为 RKNN。
- RK3568 NPU 已成功加载并运行。
- 固定 WAV 已完成 PC/TCP/RK3568/NPU/Top-K 全链路验证。
- 实时麦克风已产生过 `Speech`、`Silence`、`Clatter`、`Rustle`、`Tap` 等分类结果。
- 当前工程：[rk3568-edge-audio](https://github.com/zhenhaochi2333-dev/rk3568-edge-audio)

---

## 3. Candidate Shortlist

| 模型 | 主要任务 | 参数/规模 | 公开指标 | ONNX | RKNN/RK3568 证据 | 结论 |
|---|---|---:|---:|---|---|---|
| YAMNet `yamnet_3s` | 通用声音事件分类 | 3.7M；69.2M multiplies/0.96s | AudioSet balanced mAP 0.306 | 有 | Rockchip 官方 + 本项目实测 | **首选** |
| EfficientAT `mn04_as` | 轻量 AudioSet 分类 | 0.983M；0.11G MACs | AudioSet mAP 0.432 | 官方仓库未提供明确导出链 | 无直接证据 | 备选 1 |
| PANNs `Cnn6` | 通用 AudioSet tagging | 4.84M；权重约 23.7 MB | AudioSet mAP 0.343 | 官方未提供 RKNN 链 | 无 | 不优先 |
| CED-Tiny | AudioSet tagging | 5.5M | AS-2M mAP 0.481 | 有官方 ONNX 导出 | 无 | 研究备选 |
| BC-ResNet | Keyword Spotting | 官方参数未找到 | Speech Commands v1/v2：98.0%/98.7% | 未确认 | 无 | 淘汰 |

### 候选来源

- [TensorFlow YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet)
- [PANNs 官方仓库](https://github.com/qiuqiangkong/audioset_tagging_cnn)
- [PANNs 预训练权重](https://zenodo.org/record/3987831)
- [EfficientAT 官方仓库](https://github.com/fschmid56/EfficientAT)
- [CED 官方仓库](https://github.com/RicherMans/CED)
- [BC-ResNet 官方仓库](https://github.com/Qualcomm-AI-research/bcresnet)

---

## 4. Comparison Table

### 4.1 YAMNet

YAMNet 使用 MobileNetV1 depthwise-separable convolution，在 AudioSet-YouTube 上训练，输出 521 个声音事件类别。

官方输入和特征参数：

- Sample rate：16 kHz
- Channels：mono
- STFT window：25 ms
- STFT hop：10 ms
- Window：periodic Hann
- Mel bins：64
- Frequency range：125–7500 Hz
- Log-Mel：`log(mel + 0.001)`
- Patch：0.96 s
- Feature tensor：96×64
- 参数量：3.7M
- 计算量：69.2M multiplies per 960 ms frame
- 官方 AudioSet balanced mAP：0.306

来源：[TensorFlow YAMNet README](https://github.com/tensorflow/models/blob/master/research/audioset/yamnet/README.md)

当前 Rockchip `yamnet_3s` 包装模型：

```text
Input:  [1, 48000] float32
Audio:  16 kHz mono, 3 seconds
Output: scores [6, 521]
```

当前项目滑动窗口：

```text
Window: 3 seconds / 48000 samples
Hop: 1.5 seconds / 24000 samples
Input: PCM16 / 32768.0 -> float32
Postprocess: six frame scores averaged, then Top-K
```

### 4.2 EfficientAT `mn04_as`

EfficientAT 是基于 MobileNetV3/高效 CNN 的 AudioSet 预训练模型，使用 Transformer-to-CNN knowledge distillation 思路，在较小模型规模下获得较高 AudioSet tagging 指标。

官方模型表给出：

- `mn04_as`：0.983M 参数、0.11G MACs、mAP 0.432
- `mn05_as`：1.43M 参数、0.16G MACs、mAP 0.443
- `mn10_as`：4.88M 参数、0.54G MACs、mAP 0.471
- `dymn04_as`：1.97M 参数、0.12G MACs、mAP 0.450
- 默认分辨率：128 mel bins、10 ms hop

来源：[EfficientAT 官方仓库](https://github.com/fschmid56/EfficientAT)

本项目优先考虑 `mn04_as`，而不是 `dymn04_as`，原因是 `mn04_as` 更接近静态 MobileNetV3，预计比动态卷积版本更容易进行 RKNN 算子审计。

尚未确认：

- 当前候选的完整固定 Sample rate
- 固定 window length
- 固定 ONNX 输入 tensor shape
- RKNN 转换结果
- RK3568 实测延迟
- 权重再分发许可

### 4.3 PANNs `Cnn6`

PANNs 官方仓库提供 Cnn6、Cnn10、Cnn14 等 AudioSet 预训练模型，并支持 Audio tagging 与 Sound Event Detection。

官方配置和论文数据：

- Cnn6：4,837,455 参数
- Cnn10：5,219,279 参数
- Cnn14：80,753,615 参数
- Cnn6 AudioSet mAP：0.343
- Cnn10 AudioSet mAP：0.380
- Cnn14 AudioSet mAP：0.431
- 默认 PANNs 配置：32 kHz、window 1024、hop 320、64 mel bins、50–14000 Hz
- 官方另有 `Cnn14_16k` 权重，但没有确认 Cnn6 的对应 16 kHz 权重

来源：

- [PANNs 官方仓库](https://github.com/qiuqiangkong/audioset_tagging_cnn)
- [PANNs 论文](https://arxiv.org/abs/1912.10211)
- [PANNs 预训练权重](https://zenodo.org/record/3987831)

PANNs 的主要问题不是不能做声音事件分类，而是：

- 当前默认采样率与 PC 链路不一致。
- Cnn6 仍有较高计算量。
- 官方没有 RKNN/RK3568 直接部署链。
- 需要自己确认完整 C++ 特征复现。

### 4.4 CED-Tiny

CED 是 ICASSP 2024 的 Audio tagging 模型，官方提供 Tiny、Mini、Small、Base 四种规模。

官方数据：

- CED-Tiny：5.5M 参数、AS-20K mAP 0.365、AS-2M mAP 0.481
- CED-Mini：9.6M 参数、AS-2M mAP 0.490
- CED-Small：22M 参数、AS-2M mAP 0.496
- CED-Base：86M 参数、AS-2M mAP 0.500
- Sample rate：16 kHz
- Mel bins：64
- 官方支持 ONNX 导出
- 官方建议使用 Kaldi/native fbank 计算 Mel 特征

来源：[CED 官方仓库](https://github.com/RicherMans/CED)

主要风险：

- 官方代码仓库标记为 GPL-3.0。
- 没有 RK3568/RKNN 直接证据。
- Transformer/LayerNorm/Attention 增加 RKNN 转换风险。
- 当前公开指标与 YAMNet/PANNs 的数据设置不完全相同。

### 4.5 BC-ResNet

BC-ResNet 官方定位是 Broadcasted Residual Learning for Efficient Keyword Spotting，验证数据是 Google Speech Commands，而不是通用 AudioSet 声音事件。

官方结果：

- Speech Commands v1 top-1：98.0%
- Speech Commands v2 top-1：98.7%

来源：[BC-ResNet 官方仓库](https://github.com/Qualcomm-AI-research/bcresnet)

结论：

```text
适合：Wake Word / Keyword Spotting
不适合：当前通用声音事件检测主模型
```

---

## 5. Modern Transformer Models as Accuracy References

以下模型可以作为精度参照，但不适合作为当前第一版 RK3568 Demo：

### AST

- 16 kHz 音频
- 128 Mel bins
- 典型输入约 10 秒、1024 帧
- 527 类 AudioSet
- 官方 AudioSet single model mAP：0.459
- 纯 Transformer/Attention 结构

来源：[AST 官方仓库](https://github.com/YuanGongND/ast)

### PaSST

- 32 kHz 音频
- 128 Mel bins
- 典型输入：128×998
- 527 类 AudioSet
- 官方模型 mAP：约 0.476
- 使用 patchout 降低计算量，但仍属于 Audio Transformer

来源：[PaSST 官方仓库](https://github.com/kkoutini/PaSST)

### HTS-AT

- 约 30M 参数
- AudioSet 配置：32 kHz、527 类
- Hierarchical Swin Transformer + token-semantic module
- 官方定位同时覆盖 sound classification 和 sound event detection

来源：[HTS-AT 官方仓库](https://github.com/RetroCirce/HTS-Audio-Transformer)

### BEATs

BEATs 的 AudioSet 指标很强，但它更偏通用音频表征预训练/大模型路线，模型结构和部署复杂度均高于当前项目需要。

来源：[BEATs 官方仓库](https://github.com/microsoft/unilm/tree/master/beats)

统一结论：

```text
论文/公开精度高 ≠ RK3568 NPU 可快速部署
```

---

## 6. RKNN / RK3568 Compatibility

### 6.1 YAMNet 的直接证据

Rockchip 官方 Model Zoo 对 YAMNet 提供：

- `yamnet_3s.onnx`
- ONNX→RKNN 转换脚本
- Python RKNN 推理示例
- Linux C++ Demo
- RK3568 支持列表
- FP16 模型配置

来源：[Rockchip YAMNet 示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yamnet)

### 6.2 当前项目的实测证据

本项目在真实 RK3568 上已验证：

```text
RKNN model loaded: inputs=1 outputs=3 backend=RKNN/NPU
```

已测结果：

- 固定 WAV：约 120 ms/次推理
- 实时麦克风：约 45–121 ms/次推理
- PC → TCP → RK3568 → NPU → Top-K：已打通

### 6.3 RKNN 版本边界

Rockchip Model Zoo 的版本说明显示，Model Zoo 2.3.2 需要 RKNPU2/RKNN 2.3.2 或更高版本。

因此当前结论是：

```text
YAMNet + RKNN 2.3.2 + matching runtime：已验证
YAMNet + RKNN 1.6.0：NOT VERIFIED
```

不能因为 RKNN 1.6.0 的算子表中出现 Conv、Pool、Resize、Transpose 等算子，就直接认定完整模型一定可转换。

来源：

- [Rockchip Model Zoo README](https://github.com/airockchip/rknn_model_zoo)
- [RKNN Toolkit2 1.6.0 operator support](https://github.com/rockchip-linux/rknn-toolkit2/blob/master/doc/RKNN-Toolkit2_OP_Support-1.6.0.md)

### 6.4 其他候选的证据等级

本次未找到以下直接证据：

- EfficientAT 在 RK3568 上成功转换并运行
- PANNs 在 RK3568 上成功转换并运行
- CED-Tiny 在 RK3568 上成功转换并运行
- AST、PaSST、HTS-AT、BEATs 在 RK3568 上成功转换并运行

它们目前只能标记为：

```text
NO DIRECT RK3568 EVIDENCE
```

---

## 7. Audio Preprocessing Architecture

### 方案 A：预处理也放入模型

```text
PCM
→ ONNX 内部 STFT
→ Mel
→ Log
→ CNN
→ RKNN
```

YAMNet 当前实现更接近这个方案。

优点：

- PC/RK3568 之间只传 PCM。
- 模型输入链路简单。
- 当前工程已经跑通。
- 官方 Rockchip 示例直接支持。

缺点：

- STFT、Mel、Log 参与 RKNN 转换。
- 不同 Toolkit 版本可能出现转换差异。
- C++ 侧无法直接控制每一步特征。

### 方案 B：CPU/DSP 预处理，NPU 只跑 CNN

```text
PCM
→ C++ STFT
→ C++ Log-Mel
→ 固定 Mel tensor
→ RKNN CNN
→ Top-K
```

优点：

- NPU 模型更简单。
- 更容易做 INT8 PTQ。
- 更容易做 C++ 数值一致性测试。
- 更容易替换不同 CNN。

缺点：

- 需要自己严格复现训练时的特征流程。
- 需要新增 FFT/Mel/fbank 代码。
- 首版 Demo 工程量更大。

CED 官方仓库明确建议使用 `kaldi-native-fbank` 计算 Mel 特征，说明 CPU fbank + NPU 模型是可行的工程结构。

当前建议：

- 第一版：继续使用 YAMNet，快速建立基线。
- 第二阶段：尝试 EfficientAT `mn04_as` 的 CPU Log-Mel + NPU CNN 方案。
- 不要为了“一切上 NPU”把复杂 DSP 算子强行塞入 RKNN。

---

## 8. Accuracy vs Deployment Cost

不同模型的公开指标不能组成严格统一排行榜，原因包括：

- 类别数不同：521 或 527。
- 数据集设置不同：AudioSet balanced、AS-20K、AS-2M 等。
- Sample rate 不同。
- 输入时长不同。
- 训练增强和模型集成策略不同。
- 有的指标是 clip tagging，有的是 frame-wise detection。

参考数据：

| 模型 | 数据集/指标 | 数值 |
|---|---|---:|
| YAMNet | AudioSet balanced mAP | 0.306 |
| PANNs Cnn6 | AudioSet mAP | 0.343 |
| PANNs Cnn10 | AudioSet mAP | 0.380 |
| PANNs Cnn14 | AudioSet mAP | 0.431 |
| EfficientAT `mn04_as` | AudioSet mAP | 0.432 |
| CED-Tiny | AS-2M mAP | 0.481 |
| AST | AudioSet single model mAP | 0.459 |
| PaSST | AudioSet mAP | 0.476 |
| BEATs | AudioSet-2M mAP | 0.506 |

这些数字只能说明算法潜力，不能直接推导出 RK3568 的实时准确率。

当前取舍：

```text
YAMNet：公开精度不是最高，但部署证据最强
EfficientAT：精度/规模比最好，但需要做 RKNN 验证
CED：精度强，但存在许可证和 Transformer 风险
AST/PaSST/BEATs：适合作为精度参照，不适合首个 RK3568 Demo
```

---

## 9. Final Recommendation

```text
BEST MODEL FOR RK3568 EDGEAUDIO

Model:
    YAMNet

Exact Variant:
    yamnet_3s

Weights:
    yamnet_3s.onnx
    yamnet_3s.rknn

Deployment:
    FP16 RKNN on RK3568 NPU
```

推荐原因：

1. 适合真实声音事件，而不是只适合 Wake Word。
2. 官方输入参数完整且适配现有 16 kHz PCM 链路。
3. Rockchip 官方直接列出 RK3568 支持。
4. 当前已经完成真实板端 NPU 验证。
5. 模型规模适中，适合 RK3568。
6. 当前无需重新训练即可继续扩展 Demo。
7. 能快速形成有价值的 Linux C++、TCP、RKNN、NPU、实时性能测试项目。

YAMNet 的获胜理由是工程证据和落地速度，而不是知名度或最高论文指标。

---

## 10. Backup Models

### Backup 1：EfficientAT `mn04_as`

适用场景：

- 希望降低模型规模。
- 希望改善 AudioSet 通用声音分类指标。
- 愿意新增一次 ONNX/RKNN 转换验证。

进入下一阶段的条件：

- 完整预处理参数确认。
- PC ONNX 结果复现。
- RKNN 转换成功。
- RK3568 固定 WAV 推理成功。
- 真实麦克风误报率优于 YAMNet。
- 权重许可确认。

### Backup 2：CED-Tiny

适用场景：

- 作为高精度研究模型。
- 研究 CPU fbank + ONNX/C++ 推理。

阻塞条件：

- GPL-3.0 代码许可不适合直接作为当前公开项目默认依赖。
- 没有 RK3568/RKNN 直接证据。
- Transformer 结构需要单独做算子审计。

---

## 11. Proposed EdgeAudio Pipeline

当前首选链路：

```text
PC Microphone
    ↓
16 kHz / mono / signed PCM16
    ↓
TCP port 5700
    ↓
RK3568 C++ receiver
    ↓
3-second sliding buffer
    ↓
48000 samples
    ↓
PCM16 → float32 / 32768
    ↓
YAMNet yamnet_3s.rknn
    ↓
RKNN/NPU FP16 inference
    ↓
scores [6, 521]
    ↓
average six frame scores
    ↓
Top-K sound events
    ↓
future temporal stabilizer / event start-end
```

当前已确认的部署参数：

```text
Sample rate: 16000 Hz
Channels: 1
Window: 3 seconds
Window samples: 48000
Current hop: 1.5 seconds
Input dtype: float32
Model input: [1, 48000]
Output scores: [6, 521]
Backend: RKNN/NPU
```

---

## 12. Minimum Demo Plan

### Step 1：固定当前 YAMNet 基线

准备少量本地测试片段：

- Speech
- Quiet/Silence
- Keyboard typing
- Clap
- Knock
- Music
- Alarm
- Vehicle/environment

每类先准备 5–10 个片段，记录：

- Top-1 是否正确
- Top-3 是否包含目标类别
- 连续窗口稳定性
- 误报类别
- RK3568 端到端延迟

### Step 2：建立真实麦克风评价表

论文 mAP 不能代表当前耳机麦克风的实际效果，应记录：

```text
类别
目标声音
Top-1
Top-3
是否连续稳定
是否误报
推理耗时
```

### Step 3：只验证 EfficientAT `mn04_as`

最短路径：

```text
官方 PyTorch 权重
→ 确认完整预处理参数
→ PC 参考推理
→ ONNX
→ RKNN FP16
→ RK3568 固定 WAV
→ 与 YAMNet 对比
```

暂不建议直接尝试 AST、PaSST、HTS-AT、BEATs 或 EfficientAT 大模型。

### Step 4：以真实数据决定是否替换

只有当 EfficientAT 同时满足以下条件，才值得替换 YAMNet：

- Speech/Keyboard/Clap/Knock 更稳定。
- 误报更少。
- RKNN 转换稳定。
- RK3568 延迟没有明显变差。
- C++ 预处理可以复现。
- 权重许可适合公开项目。

---

## 13. Risks

### 13.1 许可证风险

- YAMNet 代码和权重需要分别确认许可边界。
- EfficientAT 代码为 MIT，但权重再分发条款需要单独核对。
- PANNs 代码为 MIT，预训练权重条款需要核对。
- CED 官方代码仓库为 GPL-3.0，不宜未经审查直接纳入公开项目。

### 13.2 RKNN 版本风险

当前 YAMNet 成功路径使用 RKNN 2.3.2 matching runtime，不能直接声称 RKNN 1.6.0 兼容。

### 13.3 预处理一致性风险

任何 EfficientAT、PANNs 或 CED 的迁移，都必须保证：

- Sample rate
- STFT window
- STFT hop
- Mel bins
- Frequency range
- Log/normalization
- Padding
- Window aggregation

与官方模型一致。

### 13.4 数据分布风险

AudioSet 是 YouTube 声音片段，和当前耳机麦克风、房间噪声、电脑风扇声、键盘声之间存在域差异。最终应使用真实麦克风数据建立小型评测集。

### 13.5 评分解释风险

YAMNet、PANNs、EfficientAT、CED 都属于多标签声音事件分类，输出分数不是严格意义上互斥类别的校准概率。单次 Top-1 不应直接视为确定结论，应结合连续窗口稳定器。

---

## 14. Final Conclusion

```text
首选：YAMNet yamnet_3s
备选 1：EfficientAT mn04_as
备选 2：CED-Tiny（研究型，先解决许可证和 RKNN 风险）
```

当前最合理的工程顺序是：

```text
继续完成 YAMNet 的真实声音评测
→ 建立当前基线
→ 仅对 EfficientAT mn04_as 做一次 A/B 验证
→ 用真实 RK3568 数据决定是否替换
```

本轮不建议为了追求论文最高 mAP，直接切换到 AST、PaSST、HTS-AT 或 BEATs。

