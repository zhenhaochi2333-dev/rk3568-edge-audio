# Third-party notices

| Project / model | Source | License / use | EdgeAudio use |
|---|---|---|---|
| sherpa-onnx | [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | Apache-2.0 | C++/Python ASR runtime and C API; not copied into this repository |
| Zipformer Chinese 14M | [official model release](https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23.tar.bz2) | Model archive declares Apache-2.0 | Pretrained ASR weights; downloaded by `tools/download_asr_model.ps1`, ignored by Git |
| YAMNet | [Rockchip RKNN Model Zoo](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yamnet) | See upstream model-zoo and YAMNet notices | Existing 521-class sound-event model and labels |
| Silero VAD (future replacement) | [sherpa-onnx model references](https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx) | Upstream license applies | Not required by the current deterministic RMS gate |

## What is reused

- sherpa-onnx supplies feature extraction, streaming model execution, endpoint/decoder state, and token decoding.
- Zipformer supplies pretrained Chinese ASR weights.
- YAMNet/RKNN Model Zoo supplies the pretrained sound-event model artifact and label map.

## What EdgeAudio implements

- Windows microphone/WAV PCM16 sender and reconnect behavior.
- Linux C++17 TCP receiver, `AudioRingBuffer`, VAD gate, ASR scheduling, YAMNet window scheduling, event stabilization, command parser, JSON protocol and result publisher.
- PC GUI, deployment/validation scripts, and CPU/NPU backend reporting.

Model files are not silently renamed or copied from a complete third-party demo.

