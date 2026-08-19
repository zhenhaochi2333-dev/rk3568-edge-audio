#!/usr/bin/env python3
"""Stream a 16 kHz mono PCM16 WAV in real time for deterministic testing."""

import argparse
import socket
import time
import wave


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("--host", default="192.168.77.2")
    parser.add_argument("--port", type=int, default=5700)
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--chunk-frames", type=int, default=2048)
    args = parser.parse_args()

    with wave.open(args.wav, "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16_000, 1, 2):
            raise SystemExit("WAV must be 16000 Hz, mono, 16-bit PCM")
        payloads = []
        while data := wav.readframes(args.chunk_frames):
            payloads.append(data)

    while True:
        with socket.create_connection((args.host, args.port), timeout=5) as sock:
            print(f"streaming {args.wav} to {args.host}:{args.port}", flush=True)
            for payload in payloads:
                sock.sendall(payload)
                time.sleep(len(payload) / (16_000 * 2))
        if not args.loop:
            break
        time.sleep(0.5)


if __name__ == "__main__":
    main()

