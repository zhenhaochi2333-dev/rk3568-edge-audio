#!/usr/bin/env python3
"""Real fixed-WAV Chinese ASR regression using the selected model."""

from __future__ import annotations

import argparse
import json
import time
import wave
from pathlib import Path

import numpy as np
import sherpa_onnx


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("--model-dir", default="models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23")
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    model = Path(args.model_dir)
    with wave.open(args.wav, "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise SystemExit("WAV must be 16 kHz mono PCM16")
        samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    recognizer = sherpa_onnx.OnlineRecognizer.from_transducer(
        tokens=str(model / "tokens.txt"),
        encoder=str(model / "encoder-epoch-99-avg-1.int8.onnx"),
        decoder=str(model / "decoder-epoch-99-avg-1.onnx"),
        joiner=str(model / "joiner-epoch-99-avg-1.int8.onnx"),
        num_threads=args.threads,
        model_type="zipformer",
        enable_endpoint_detection=True,
    )
    stream = recognizer.create_stream()
    started = time.perf_counter()
    chunk = 3200
    for offset in range(0, len(samples), chunk):
        stream.accept_waveform(16000, samples[offset:offset + chunk])
        while recognizer.is_ready(stream):
            recognizer.decode_stream(stream)
    stream.input_finished()
    while recognizer.is_ready(stream):
        recognizer.decode_stream(stream)
    elapsed = time.perf_counter() - started
    result = recognizer.get_result(stream)
    print(json.dumps({"text": result, "audio_seconds": len(samples) / 16000,
                      "elapsed_seconds": elapsed,
                      "rtf": elapsed / max(1e-9, len(samples) / 16000),
                      "backend": "VM ONNX CPU"}, ensure_ascii=False, indent=2))
    if not result.strip():
        raise SystemExit("ASR produced empty text")


if __name__ == "__main__":
    main()

