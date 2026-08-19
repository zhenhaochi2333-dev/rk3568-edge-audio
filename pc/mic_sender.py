#!/usr/bin/env python3
"""Capture the default Windows microphone and stream PCM16 to RK3568."""

import argparse
import socket
import time

import pyaudio


RATE = 16_000
CHANNELS = 1
FORMAT = pyaudio.paInt16
CHUNK = 2048


def connect(host: str, port: int) -> socket.socket:
    while True:
        try:
            sock = socket.create_connection((host, port), timeout=5)
            sock.settimeout(None)
            print(f"connected to {host}:{port}", flush=True)
            return sock
        except OSError as exc:
            print(f"connect failed: {exc}; retrying in 1 s", flush=True)
            time.sleep(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.77.2")
    parser.add_argument("--port", type=int, default=5700)
    parser.add_argument(
        "--device",
        type=int,
        default=6,
        help="PyAudio input device index (default: 6, current XIBERIA headset mic)",
    )
    args = parser.parse_args()

    audio = pyaudio.PyAudio()
    stream = audio.open(
        format=FORMAT,
        channels=CHANNELS,
        rate=RATE,
        input=True,
        input_device_index=args.device,
        frames_per_buffer=CHUNK,
    )
    print("capturing 16000 Hz mono signed 16-bit PCM; press Ctrl+C to stop", flush=True)

    try:
        sock = connect(args.host, args.port)
        try:
            while True:
                sock.sendall(stream.read(CHUNK, exception_on_overflow=False))
        finally:
            sock.close()
    except KeyboardInterrupt:
        pass
    finally:
        stream.stop_stream()
        stream.close()
        audio.terminate()


if __name__ == "__main__":
    main()
