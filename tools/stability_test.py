#!/usr/bin/env python3
"""Repeat a real PCM WAV TCP session to exercise reconnect and receiver lifetime."""

from __future__ import annotations

import argparse
import json
import socket
import threading
import time
import wave
from pathlib import Path


def read_wav(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise SystemExit("WAV must be 16 kHz mono PCM16")
        return wav.readframes(wav.getnframes())


def drain(sock: socket.socket, bucket: list[bytes], stop: threading.Event) -> None:
    sock.settimeout(0.5)
    while not stop.is_set():
        try:
            data = sock.recv(8192)
        except socket.timeout:
            continue
        except OSError:
            return
        if not data:
            return
        bucket.append(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--audio-port", type=int, default=5700)
    parser.add_argument("--result-port", type=int, default=5701)
    parser.add_argument("--duration", type=float, default=1800.0)
    parser.add_argument("--interval", type=float, default=5.0)
    args = parser.parse_args()
    pcm = read_wav(Path(args.wav))
    started = time.monotonic()
    cycle = 0
    while time.monotonic() - started < args.duration:
        cycle += 1
        result = socket.create_connection((args.host, args.result_port), timeout=5)
        received: list[bytes] = []
        stop = threading.Event()
        reader = threading.Thread(target=drain, args=(result, received, stop), daemon=True)
        reader.start()
        audio = socket.create_connection((args.host, args.audio_port), timeout=5)
        send_started = time.monotonic()
        for offset in range(0, len(pcm), 640):
            audio.sendall(pcm[offset:offset + 640])
            time.sleep(0.02)
        audio.shutdown(socket.SHUT_WR)
        audio.close()
        time.sleep(2.0)
        stop.set()
        result.close()
        reader.join(timeout=1.0)
        text = b"".join(received).decode("utf-8", "replace")
        finals = sum(1 for line in text.splitlines() if '"final":true' in line)
        events = sum(1 for line in text.splitlines() if '"type":"sound_event"' in line)
        print(json.dumps({"cycle": cycle, "send_seconds": round(time.monotonic() - send_started, 2),
                          "final_asr": finals, "sound_events": events}, ensure_ascii=False), flush=True)
        time.sleep(max(0.0, args.interval))


if __name__ == "__main__":
    main()
