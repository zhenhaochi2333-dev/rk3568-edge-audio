#!/usr/bin/env python3
"""Real sherpa-onnx Chinese ASR TCP service for PC/VM validation.

The first validation mode is utterance-level: the sender closes the TCP
session at the end of a test utterance, then the real streaming Zipformer
model decodes the buffered PCM. The formal Linux C++ core keeps streaming
decoding enabled through the sherpa-onnx C API.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

import numpy as np
import sherpa_onnx

RATE = 16_000
FRAME = 320


class Publisher:
    def __init__(self, port: int):
        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("0.0.0.0", port))
        self.server.listen(1)
        self.client = None
        self.lock = threading.Lock()
        self.stop_event = threading.Event()

    def start(self):
        threading.Thread(target=self._accept_loop, daemon=True).start()

    def _accept_loop(self):
        while not self.stop_event.is_set():
            try:
                client, _ = self.server.accept()
            except OSError:
                return
            with self.lock:
                if self.client:
                    self.client.close()
                self.client = client
            print("RESULT_CLIENT_CONNECTED", flush=True)

    def publish(self, item: dict):
        payload = (json.dumps(item, ensure_ascii=False) + "\n").encode("utf-8")
        with self.lock:
            if not self.client:
                return
            try:
                self.client.sendall(payload)
            except OSError:
                self.client.close()
                self.client = None

    def close(self):
        self.stop_event.set()
        self.server.close()
        with self.lock:
            if self.client:
                self.client.close()
                self.client = None


class EnergyVad:
    def __init__(self):
        self.active = False
        self.start_count = 0
        self.end_count = 0

    def process(self, frame: np.ndarray):
        rms = float(np.sqrt(np.mean(np.square(frame)))) if len(frame) else 0.0
        if self.active:
            self.end_count = self.end_count + 1 if rms < 0.010 else 0
            ended = self.end_count >= 12
            if ended:
                self.active = False
                self.end_count = 0
            return self.active, rms, False, ended
        self.start_count = self.start_count + 1 if rms >= 0.018 else 0
        started = self.start_count >= 3
        if started:
            self.active = True
            self.start_count = 0
        return self.active, rms, started, False


def make_recognizer(model_dir: Path, threads: int):
    return sherpa_onnx.OnlineRecognizer.from_transducer(
        tokens=str(model_dir / "tokens.txt"),
        encoder=str(model_dir / "encoder-epoch-99-avg-1.int8.onnx"),
        decoder=str(model_dir / "decoder-epoch-99-avg-1.onnx"),
        joiner=str(model_dir / "joiner-epoch-99-avg-1.int8.onnx"),
        num_threads=threads,
        model_type="zipformer",
        enable_endpoint_detection=True,
    )


def decode_audio(recognizer, samples: np.ndarray) -> str:
    stream = recognizer.create_stream()
    for offset in range(0, len(samples), 3200):
        stream.accept_waveform(RATE, samples[offset:offset + 3200])
        while recognizer.is_ready(stream):
            recognizer.decode_stream(stream)
    stream.input_finished()
    while recognizer.is_ready(stream):
        recognizer.decode_stream(stream)
    try:
        return recognizer.get_result(stream).strip()
    except IndexError:
        return ""


def decode_external(model_dir: Path, samples: np.ndarray) -> tuple[str, float, float]:
    """Run the same real ASR regression entry point in a clean process.

    This avoids state/allocator interactions between the long-lived TCP thread
    and the native ONNX binding on Windows. It is still the real model, never
    a mock or keyword table.
    """
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as handle:
        path = handle.name
    try:
        pcm = np.clip(samples * 32768.0, -32768, 32767).astype("<i2")
        with wave.open(path, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(pcm.tobytes())
        command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools" / "asr_file_test.py"),
                   path, "--model-dir", str(model_dir)]
        child_env = os.environ.copy()
        child_env["PYTHONIOENCODING"] = "utf-8"
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                env=child_env, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"ASR child failed ({result.returncode}): {result.stderr or result.stdout}")
        item = json.loads(result.stdout)
        return item["text"], float(item["elapsed_seconds"]) * 1000.0, float(item["rtf"])
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def command_for(text: str) -> str:
    if "开始监控" in text or "启动监控" in text:
        return "START_MONITORING"
    if "停止监控" in text or "结束监控" in text:
        return "STOP_MONITORING"
    if "系统状态" in text or "查看状态" in text:
        return "QUERY_STATUS"
    return ""


def serve(args):
    model_dir = Path(args.model_dir).resolve()
    publisher = Publisher(args.result_port)
    publisher.start()
    audio_server = socket.socket()
    audio_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    audio_server.bind(("0.0.0.0", args.audio_port))
    audio_server.listen(1)
    print(f"EDGEAUDIO_READY audio_port={args.audio_port} result_port={args.result_port} "
          "yamnet_backend=VM MOCK asr_backend=VM ONNX CPU", flush=True)
    try:
        while True:
            client, address = audio_server.accept()
            print(f"AUDIO_CLIENT_CONNECTED {address}", flush=True)
            chunks = []
            pending = bytearray()
            vad = EnergyVad()
            total = 0
            try:
                while True:
                    data = client.recv(8192)
                    if not data:
                        break
                    pending.extend(data)
                    usable = len(pending) - (len(pending) % 2)
                    pcm = np.frombuffer(pending[:usable], dtype="<i2").astype(np.float32) / 32768.0
                    del pending[:usable]
                    chunks.append(pcm)
                    for offset in range(0, len(pcm) - FRAME + 1, FRAME):
                        active, rms, started, ended = vad.process(pcm[offset:offset + FRAME])
                        timestamp = total * 1000 // RATE
                        if started:
                            publisher.publish({"type": "vad", "timestamp_ms": timestamp,
                                               "state": "speech_start", "rms": round(rms, 4)})
                        if ended:
                            publisher.publish({"type": "vad", "timestamp_ms": timestamp,
                                               "state": "speech_end", "rms": round(rms, 4)})
                        total += FRAME
            finally:
                client.close()
            audio = np.concatenate(chunks) if chunks else np.zeros(0, dtype=np.float32)
            if len(audio):
                digest = hashlib.sha256(np.clip(audio * 32768.0, -32768, 32767).astype('<i2').tobytes()).hexdigest()
                print(f"AUDIO_BUFFER samples={len(audio)} min={audio.min():.4f} max={audio.max():.4f} sha256={digest}", flush=True)
                try:
                    text, elapsed_ms, model_rtf = decode_external(model_dir, audio)
                except Exception as error:
                    # Keep the TCP endpoint alive if the Windows native binding
                    # rejects one child-process invocation. The error is visible
                    # to the GUI and is never converted into a fake transcript.
                    message = str(error)
                    print(f"ASR_ERROR {message}", flush=True)
                    publisher.publish({"type": "status", "state": "asr_error",
                                       "error": message, "backend": "VM ONNX CPU"})
                else:
                    item = {"type": "asr", "timestamp_ms": len(audio) * 1000 // RATE,
                            "text": text, "final": True, "latency_ms": round(elapsed_ms, 2),
                            "rtf": round(model_rtf, 3),
                            "backend": "VM ONNX CPU"}
                    command = command_for(text)
                    if command:
                        item["command"] = command
                    print(json.dumps(item, ensure_ascii=False), flush=True)
                    publisher.publish(item)
            print("AUDIO_CLIENT_DISCONNECTED", flush=True)
    finally:
        audio_server.close()
        publisher.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-dir", default="models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23")
    parser.add_argument("--audio-port", type=int, default=5700)
    parser.add_argument("--result-port", type=int, default=5701)
    parser.add_argument("--threads", type=int, default=2)
    serve(parser.parse_args())


if __name__ == "__main__":
    main()
