# Chinese ASR selection for EdgeAudio (2026-08)

## Decision

Formal ASR: **sherpa-onnx streaming Zipformer Chinese 14M** (`sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23`).

VM backend: ONNX Runtime CPU. Board target: RKNN encoder/joiner experiment with CPU decoder; keep ARM CPU as the measured fallback. The final backend is decided from RK3568 RTF, latency, CPU load and memory, not from theoretical NPU TOPS.

## Shortlist

| Candidate | Chinese | Streaming / C++ | ARM64 | RKNN evidence | Decision |
|---|---|---|---|---|---|
| sherpa-onnx Zipformer 14M | Yes | Mature online recognizer and C API | Yes | sherpa-onnx documents RKNN and RK3568 support; model conversion still needs board validation | **Selected** |
| sherpa-onnx SenseVoice int8 | Mandarin plus English/Japanese/Korean/Cantonese | C++ API, primarily offline/utterance with VAD | Yes | RKNN models and RK3568 target path exist, but model-version compatibility matters | Fallback for richer multilingual/utterance use |
| Whisper / whisper.cpp or sherpa-onnx Whisper | Chinese and multilingual | C++/ARM possible | Yes | RKNN conversion has dynamic-shape/encoder-decoder risks and larger deployment cost | Rejected for tonight |

## Why Zipformer 14M

- The release documents a fixed 16 kHz, 80-dimensional feature path and provides FP32/int8 encoder/joiner artifacts.
- It supports streaming state, endpoint detection and C++/C APIs through sherpa-onnx.
- The model is small enough for the first VM and ARM fallback, while the neural encoder/joiner can remain an explicit future NPU split.
- A real fixed-WAV run on this PC produced Chinese text: `对我做了介绍那么我想说的是大家如果对我的研究感兴趣`.

## Architecture review

```text
PCM16 -> CPU feature extraction / VAD -> Zipformer encoder (CPU now; RKNN experiment tomorrow)
      -> joiner / decoder / tokenization on CPU -> Chinese text -> command parser
```

Attention, LayerNorm, dynamic state and decoder control flow remain inside the selected runtime or CPU path. Beam search, tokenizer and command rules are not forced onto the NPU.

## Sources

- [sherpa-onnx repository](https://github.com/k2-fsa/sherpa-onnx)
- [online transducer models and Chinese Zipformer 14M parameters](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html)
- [sherpa-onnx RKNN documentation](https://k2-fsa.github.io/sherpa/onnx/rknn/index.html)
- [sherpa-onnx SenseVoice documentation](https://k2-fsa.github.io/sherpa/onnx/sense-voice/index.html)
- [sherpa-onnx Whisper RKNN conversion notes](https://github.com/k2-fsa/sherpa-onnx/blob/master/scripts/whisper/rknn/README.md)
