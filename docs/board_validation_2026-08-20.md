# RK3568 board validation — 2026-08-20

Target: `root@192.168.77.2`, hostname `topeet`, `aarch64`, Linux 4.19.232.

## Build

The ARM64 sherpa-onnx 1.13.2 C API was built locally on the board with
ONNX Runtime 1.16.3 and RKNN runtime support. The EdgeAudio receiver then
compiled and linked with both backends enabled:

```text
EdgeAudio RKNN backend: ON
EdgeAudio sherpa-onnx ASR backend: ON
Built target audio_receiver
ELF 64-bit LSB shared object, ARM aarch64
librknnrt.so => /lib/librknnrt.so
libsherpa-onnx-c-api.so => package lib/ at runtime
```

The sherpa shared runtime contains `libsherpa-onnx-c-api.so`,
`libsherpa-onnx-cxx-api.so`, and `libonnxruntime.so`. The ASR model is the
official `sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23` release.

## Full board pipeline

The official 5.61 s Chinese test WAV was sent as fragmented 16 kHz PCM16 over
TCP to the current C++ receiver. The receiver used real YAMNet RKNN and real
sherpa-onnx CPU ASR:

```text
EDGEAUDIO_READY audio_port=5742 result_port=5743 yamnet_backend=rknn asr_backend=cpu
VAD: speech_start / speech_end
ASR partial: 我做了介绍
ASR partial: 我想说的是
ASR final: 如果对我的研究感兴趣
```

YAMNet produced real Top-5 results with `Speech` as the top class. Four
windows measured `42.60--56.66 ms` inference time and emitted
`backend=rknn`, including `EVENT_START` for stable speech. This is a real
RKNN/NPU result, not a mock event.

The C++ ASR CPU fallback is functional and returns real Chinese text, but its
20 ms feed callbacks measured approximately `3.5--7.4` RTF. It is therefore a
valid fallback, not yet a real-time board solution; the ASR RKNN/hybrid model
conversion and A/B measurement remain open.

## Thermal protection

The board build and runtime were wrapped by `thermal_guard.sh`. The observed
SoC temperature stayed below `58 °C` during compilation and was about `45 °C`
after the full pipeline test. The guard pauses the child at `78 °C` and
resumes at `68 °C`.

## Remaining board work

1. Convert and operator-validate the selected Zipformer neural network for
   RKNN, or keep the measured CPU fallback if NPU conversion is not worthwhile.
2. Repeat CPU versus hybrid measurements with CPU load, memory, latency and
   RTF after the final model split.
3. Validate the packaged deployment path with the real microphone and PC GUI.
