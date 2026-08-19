# RK3568 EdgeAudio

Minimal real-time sound-event demo for the RK3568:

```text
Windows microphone -> 16 kHz mono PCM16 -> TCP:5700 -> RK3568 -> YAMNet -> top-3 classes
```

## Model

- Model: YAMNet `yamnet_3s.onnx`, Rockchip RKNN Model Zoo build
- Source: [Rockchip RKNN Model Zoo](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yamnet)
- License: Apache-2.0 (YAMNet / model-zoo code)
- Input: `[1, 48000]` float32 waveform, 3 seconds at 16 kHz
- Preprocessing: signed PCM16 divided by `32768.0`; no extra feature extraction in the application because the ONNX model contains the YAMNet log-mel frontend
- Output: `[6, 521]`; average the six frame scores and print top 3 AudioSet labels
- Why selected: lightweight MobileNet-based sound classifier, public weights, native 16 kHz input, and Rockchip documents RK3568 support for this exact model

The model and AudioSet label map are stored under `models/`. The public regression sample is `tests/data/speech_whistling2.wav`; it is not a private recording.

## Build the board receiver

The board build expects the ARM64 RKNN runtime staged in `deps/rknn_runtime_1.6.0/`.

```bash
./tools/build_board.sh
```

The checked-in `yamnet_3s.rknn` was converted on the ARM64 Ubuntu 20.04 board environment with RKNN-Toolkit2 2.3.2 for `rk3568`, then executed with the matching 2.3.2 runtime.

Run on RK3568:

```bash
./build/audio_receiver /root/rk3568-edge-audio/models/yamnet_3s.rknn \
  /root/rk3568-edge-audio/models/yamnet_class_map.csv 5700
```

## PC sender

Install the only host capture dependency:

```powershell
.\tools\install_pc_deps.ps1
```

Start the real microphone sender:

```powershell
python .\pc\mic_sender.py --host 192.168.77.2 --port 5700
```

The sender defaults to PyAudio device `6`, the currently connected XIBERIA headset microphone. Override it with `--device N` if Windows renumbers the audio device.

For deterministic network/model testing, stream the public sample instead:

```powershell
python .\pc\wav_sender.py .\tests\data\speech_whistling2.wav --host 192.168.77.2 --port 5700
```

Then make sounds for 30–60 seconds: speak, type, clap, and stay quiet. The receiver emits one result every 1.5 seconds after its 3-second window is full.

## Verified status

- PC ONNX reference inference: verified on the public whistling WAV.
- TCP receiver/sender: verified with the public WAV and the live PC microphone.
- Backend: RKNN/NPU verified on the RK3568; fixed-WAV inference was about 120 ms and live windows were about 43–121 ms.
- Live microphone: real input produced `Cacophony`, `Tools`, `Power tool`, `Clatter`, `Rustle`, and `Tap` classifications before returning to `Silence`. Speech was not isolated clearly enough to claim a speech-specific response.
- Existing `rk3568-edge-vision` repositories: read-only and not modified by this project.

## Next step

Convert `models/yamnet_3s.onnx` with RKNN-Toolkit2 for `rk3568`, deploy the `.rknn` plus receiver to the board, and run the deterministic WAV test before live microphone testing.
