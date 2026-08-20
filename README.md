# RK3568 EdgeAudio

端侧实时音频工程：Windows/耳机麦克风输入 16 kHz 单声道 PCM16，通过 TCP 送入 Linux C++17 核心，同时驱动 VAD、中文 ASR 和 YAMNet 声音事件分支，最后以 newline-delimited JSON 回传 PC GUI。

```text
Windows Mic / WAV
  -> PCM16 16 kHz mono TCP:5700
  -> C++ TcpAudioReceiver / PCM16 framing
  -> AudioStreamBuffer (3 s / 1.5 s hop)
      -> 20 ms Speech Gate -> streaming Chinese ASR
      -> YamnetRknnModel -> YamnetPostProcessor -> RKNN/NPU or VM mock
  -> JSON TCP:5701
  -> Windows Tk GUI
```

The formal C++ path keeps the data flow small and explicit:

```text
rk3568/audio_receiver.cpp       lifecycle + VAD/ASR/YAMNet scheduling
rk3568/tcp_audio_receiver.cpp   POSIX socket + arbitrary-byte PCM16 stream
rk3568/yamnet_rknn.cpp          RKNN metadata, run and RAII resource lifetime
rk3568/yamnet_postprocess.cpp   6-frame average, Top-K and labels
include/edgeaudio/audio_stream_buffer.h
                                 bounded absolute sample timeline
```

## Project decision

- Sound event: YAMNet `yamnet_3s`，输入 `[1, 48000]` float32，3 s window / 1.5 s hop，521 AudioSet classes。
- ASR: sherpa-onnx streaming Zipformer Chinese 14M，`encoder-epoch-99-avg-1.int8.onnx` + FP32 decoder/joiner，16 kHz，C++ C API。
- Formal ASR backend: sherpa-onnx CPU；ASR RKNN/Hybrid remains an isolated experiment and is not part of the Demo runtime。
- VAD: C++/PC deterministic 20 ms RMS gate，独立于 YAMNet；后续可替换为 Silero/WebRTC，不改变调度协议。
- `YAMNet` VM backend defaults to explicit `VM MOCK` in the current C++ receiver because the checked-in YAMNet ONNX frontend is not linked into the C++ VM build. The board backend is `RKNN/NPU` and must be validated on RK3568.

## ASR model setup

The model archive is public and intentionally ignored by Git because it is large.

```powershell
python -m pip install -r .\pc\requirements.txt
.\tools\download_asr_model.ps1
python .\tools\asr_file_test.py .\models\asr\sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23\test_wavs\0.wav
python .\tools\asr_regression.py --output .\logs\asr_regression.json
```

The fixed-WAV test must print non-empty Chinese text and an RTF. It is a real model pass; no mock text is accepted.
The regression command runs the public `test_wavs/0.wav` and `1.wav` files
downloaded with the official sherpa-onnx model release, and checks the bundled
`8k.wav` negative case is rejected because EdgeAudio requires 16 kHz mono
PCM16. The current PC results are real Chinese transcripts with ONNX CPU RTF
about 0.013--0.016 on both valid files.

## Linux C++ build

Build sherpa-onnx with its C API on Ubuntu/ARM64 or cross-compile it, then point CMake at the install prefix:

```bash
cmake -S rk3568 -B build \
  -DSHERPA_ONNX_ROOT=/opt/sherpa-onnx \
  -DRKNN_ROOT=$PWD/deps/rknn_runtime_2.3.2 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The prefix must contain `include/sherpa-onnx/c-api/c-api.h` and `lib/libsherpa-onnx-c-api.so`. The current Windows check compiles the portable C++ ASR wrapper only; `audio_receiver` is POSIX/Linux code.

Build and run the dependency-free C++ regression tests:

```powershell
cmake -S rk3568 -B build-windows -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows --config Release --target edgeaudio_tests edgeaudio_host_check
ctest --test-dir build-windows -C Release --output-on-failure
```

The tests cover arbitrary TCP byte splits, including odd-byte PCM16 framing, the
3 s / 1.5 s rolling window, circular-buffer overwrite behavior, Top-K ordering,
ties, empty input, oversized K and AudioSet label mapping.

For a repeatable ARM64 sherpa build with RKNN headers, ONNX Runtime and the
same thermal guard, use `tools/build_sherpa_rk3568.sh` with
`SHERPA_ONNX_SOURCE`, `ONNXRUNTIME_ROOT`, and optionally `RKNN_ROOT` set.

Run in VM with the real ASR model and explicit YAMNet mock status:

```bash
./build/audio_receiver \
  --labels models/yamnet_class_map.csv \
  --yamnet-backend mock \
  --asr-backend cpu \
  --asr-tokens models/asr/.../tokens.txt \
  --asr-encoder models/asr/.../encoder-epoch-99-avg-1.onnx \
  --asr-decoder models/asr/.../decoder-epoch-99-avg-1.onnx \
  --asr-joiner models/asr/.../joiner-epoch-99-avg-1.onnx
```

## PC validation

The Python service is a Windows/VM validation adapter using the same official sherpa-onnx model. It buffers one TCP utterance, runs real ASR, and publishes final JSON. This keeps the PC validation reliable while the formal Linux C++ path remains streaming.

```powershell
python .\pc\asr_tcp_service.py
python .\pc\edgeaudio_gui.py --host 127.0.0.1 --input-device auto
python .\pc\mic_sender.py --host 127.0.0.1 --port 5700 --device auto
```

For deterministic TCP testing:

```powershell
python .\pc\wav_sender.py .\models\asr\sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23\test_wavs\0.wav --host 127.0.0.1 --port 5700
```

`mic_sender.py` defaults to a compatible `K30S`/`K03S` headset microphone entry. On Windows it automatically prefers FFmpeg DirectShow for this device and emits the fixed 16 kHz mono PCM16 wire format; PyAudio remains the fallback. It accepts `--device N` for an explicit index. Use `python pc/mic_sender.py --list-devices` to verify the selected device.

## Board scripts

```powershell
.\tools\deploy_rk3568.ps1 -BoardHost 192.168.77.2 -BoardUser root
.\tools\validate_board.ps1 -BoardHost 192.168.77.2 -BoardUser root
.\tools\start_edgeaudio.ps1 -Mode board -BoardHost 192.168.77.2
.\tools\stop_edgeaudio.ps1
```

When the C++ receiver is linked to sherpa-onnx, pass its install prefix so the
deployment package carries `libsherpa-onnx-c-api.so` and `libonnxruntime.so`:

```powershell
.\tools\deploy_rk3568.ps1 -BoardHost 192.168.77.2 -SherpaOnnxRoot C:\path\to\sherpa-onnx\install
```

The formal board package uses the supplied CPU ASR model and the validated YAMNet RKNN model. The ASR RKNN/Hybrid experiment is intentionally kept outside this Demo until continuous-cache accuracy and performance A/B are complete.

Board build and board runtime are wrapped by `tools/thermal_guard.sh`. It
monitors the SoC thermal zone, pauses the EdgeAudio process at 78 °C, and
resumes it after cooling to 68 °C. Thresholds and polling interval can be
overridden with `EDGEAUDIO_THERMAL_PAUSE_C`, `EDGEAUDIO_THERMAL_RESUME_C`,
and `EDGEAUDIO_THERMAL_POLL_S`.

## JSON protocol

ASR:

```json
{"type":"asr","timestamp_ms":3200,"text":"现在开始测试端侧语音识别","final":true,"latency_ms":120.0,"rtf":0.3,"backend":"ARM CPU"}
```

Sound event:

```json
{"type":"sound_event","timestamp_ms":3000,"topk":[{"index":0,"label":"Speech","score":0.9}],"stable_event":"Speech","transition":"EVENT_START","inference_ms":42.0,"backend":"RKNN/NPU"}
```

Status messages include `audio_rms`; VAD start/end messages include the frame RMS so the GUI can show a live input level. No confidence is fabricated for ASR.

## Testing status

- Real fixed-WAV Chinese ASR: PASS on Windows ONNX CPU; both official 16 kHz Chinese samples produced non-empty Chinese text with RTF 0.013--0.016.
- C++17 portable ASR wrapper host compile: PASS with MSVC/CMake.
- PC GUI/protocol/scripts: Python syntax checked; GUI shows connection, RMS level, VAD, Top-5 sound event, ASR partial/final text, command action, backend and latency.

Previously verified RK3568 hardware results, recorded before the current
engineering refactor:

- C++ Linux receiver: PASS with real TCP PCM, VAD, sherpa-onnx C API CPU ASR and YAMNet RKNN.
- YAMNet RKNN: PASS; full pipeline measured 42.60--56.66 ms per 3 s window.
- Board CPU ASR: real Chinese partial/final text PASS; measured 20 ms feed RTF was 3.5--7.4. This is the formal fallback and is not replaced by the isolated ASR RKNN experiment.
- Formal `/root/edgeaudio` runtime package: fixed-WAV TCP acceptance PASS with bundled ARM64 shared libraries and thermal guard.

The board results above are historical evidence recorded from earlier sessions.
The current cleanup session could not reconnect to `192.168.77.2:22`, so new
RK3568 runtime, RKNN performance, and live-microphone claims are **NOT
REVALIDATED ON BOARD IN THIS SESSION**. The Windows CMake/unit test and
portable syntax checks are local validation only.

The board result is recorded in `docs/board_validation_2026-08-20.md`: the
current isolated board build has both C++ backends enabled, and the full
pipeline produced real YAMNet RKNN Top-5 plus real Chinese ASR text. CPU ASR
is a verified fallback but not yet real-time on this board. The separate ASR RKNN
experiment has its own runtime diagnosis and is not merged into the Demo.

## Final Demo polish

The intended interview demonstration is deliberately small. In board mode, the RK3568 also launches a local GTK monitor on its Xorg display (`DISPLAY=:0`). A board-side result relay fans out the unchanged JSON from the formal `5701` publisher to `5702`, so the PC GUI and board monitor can read the same results simultaneously without another inference pipeline.

1. Start the board core and the PC GUI.
2. Confirm the green connection state on both the PC GUI and the RK3568 local monitor, plus the live activity bar and board temperature.
3. Say `开始监控`; show the Chinese transcript and `START_MONITORING | Monitoring Started`.
4. Create speech, tapping or music; show VAD and the stable YAMNet event/Top-5 list.
5. Say `查看状态` or `停止监控`; show the parsed command and backend/latency fields.

Detailed operator steps are in [docs/demo_script.md](docs/demo_script.md). The current
formal state is recorded in [PROJECT_STATUS.md](PROJECT_STATUS.md). The ASR RKNN
experiment remains on the separate `feature/asr-rknn-hybrid` branch and is not part
of this formal Demo branch.
The board-local GTK monitor is documented in [docs/board_gui_report.md](docs/board_gui_report.md).
The 30-minute reconnect and thermal-guard result is recorded in
[docs/stability_test_report.md](docs/stability_test_report.md).

## Engineering differences

Third-party runtime/model code is isolated behind `AsrEngine` and documented in `THIRD_PARTY.md`. EdgeAudio owns the TCP PCM16 framing, bounded sample timeline, VAD gate, YAMNet window/postprocess/stabilizer, result fusion JSON, command parser, GUI, reconnect behavior, deployment scripts, and performance fields.

More detailed engineering decisions are in [docs/engineering.md](docs/engineering.md).

## Scope boundary

No speaker recognition, TTS, LLM, agent, independent KWS, database, web UI, or training pipeline is included in this first version.
