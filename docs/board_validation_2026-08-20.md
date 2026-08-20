# RK3568 board validation — 2026-08-20

Target: `root@192.168.77.2`, hostname `topeet`, `aarch64`, Linux 4.19.232.

## Build

The current C++17 receiver was compiled on the board with the checked-in RKNN
2.3.2 headers/runtime:

```text
EdgeAudio RKNN backend: ON
EdgeAudio sherpa-onnx ASR backend: OFF
Built target audio_receiver
ELF 64-bit LSB shared object, ARM aarch64
librknnrt.so => /lib/librknnrt.so
```

The normal command intentionally fails when the C++ sherpa runtime is absent:

```text
EDGEAUDIO_FATAL ASR unavailable: ASR runtime not linked; configure SHERPA_ONNX_ROOT and rebuild
EXIT=1
```

This prevents a board run without ASR from being reported as a full system
pass.

## YAMNet/RKNN and protocol

Using `--allow-asr-unavailable` only for this diagnostic branch, the board
accepted PCM16 TCP input, generated VAD/status messages, ran the real YAMNet
RKNN model, and returned JSON Top-5 results:

```text
EDGEAUDIO_READY audio_port=5730 result_port=5731 yamnet_backend=rknn asr_backend=UNAVAILABLE
sound_event timestamp_ms=3000 backend=rknn inference_ms=129.20
sound_event timestamp_ms=4500 backend=rknn inference_ms=127.76 stable_event=Speech transition=EVENT_START
status yamnet_backend=rknn asr_backend=UNAVAILABLE
```

The input was the official 5.61 s Zipformer test WAV, sent as fragmented TCP
PCM. VAD transitions and Top-5 labels were returned successfully. This is a
real board YAMNet NPU/protocol pass, not a mock event.

## Remaining board work

Install or build the ARM64 sherpa-onnx C API shared runtime, rebuild with
`-DSHERPA_ONNX_ROOT=<prefix>`, then rerun the same pipeline with CPU ASR and
compare CPU versus RKNN/hybrid ASR RTF, latency, CPU load, and memory. The
selected Chinese Zipformer model is already prepared locally and is ignored
from Git as a large deployment asset.

