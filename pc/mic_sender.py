#!/usr/bin/env python3
"""Capture the default Windows microphone and stream PCM16 to RK3568."""

import argparse
import audioop
import os
import shutil
import socket
import subprocess
import time

import pyaudio

from audio_device import list_input_devices, select_input_device


RATE = 16_000
CHANNELS = 1
FORMAT = pyaudio.paInt16
CHUNK = 2048


def pcm_to_output_rate(data: bytes, native_rate: int, state):
    """Convert native Windows capture PCM16 to the fixed 16 kHz wire rate."""
    if native_rate == RATE:
        return data, state
    converted, state = audioop.ratecv(
        data, 2, CHANNELS, native_rate, RATE, state
    )
    return converted, state


def start_ffmpeg_capture(device_name: str):
    """Capture a Windows DirectShow device and emit the fixed wire format."""
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-f", "dshow", "-i", "audio=" + device_name,
        "-ac", str(CHANNELS), "-ar", str(RATE),
        "-f", "s16le", "-",
    ]
    return subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )


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
    parser.add_argument("--device", default="auto",
                        help="input device index, or auto to prefer K30S/K03S")
    parser.add_argument("--capture-backend", choices=("auto", "ffmpeg", "pyaudio"),
                        default="auto",
                        help="Windows capture backend (auto prefers FFmpeg DirectShow)")
    parser.add_argument(
        "--list-devices",
        action="store_true",
        help="list input devices and exit",
    )
    args = parser.parse_args()

    audio = pyaudio.PyAudio()
    if args.list_devices:
        for device in list_input_devices(audio):
            print(f"{device.index}: {device.name} [{device.host_api}]", flush=True)
        selected = select_input_device(audio, args.device)
        print(f"AUTO_SELECTED: {selected.display_name}", flush=True)
        audio.terminate()
        return
    selected = select_input_device(audio, args.device)
    print(f"selected input: {selected.display_name}", flush=True)
    ffmpeg_process = None
    stream = None
    use_ffmpeg = (
        args.capture_backend == "ffmpeg"
        or (args.capture_backend == "auto" and os.name == "nt" and shutil.which("ffmpeg"))
    )
    if use_ffmpeg:
        ffmpeg_process = start_ffmpeg_capture(selected.name)
        print(
            f"capturing via FFmpeg DirectShow: {selected.name} -> "
            f"{RATE} Hz mono signed 16-bit PCM; press Ctrl+C to stop",
            flush=True,
        )
    else:
        native_rate = int(round(selected.sample_rate or RATE))
        if native_rate < RATE:
            native_rate = RATE
        native_chunk = max(1, round(CHUNK * native_rate / RATE))
        stream = audio.open(
            format=FORMAT,
            channels=CHANNELS,
            rate=native_rate,
            input=True,
            input_device_index=selected.index,
            frames_per_buffer=native_chunk,
        )
        print(
            f"capturing {native_rate} Hz -> {RATE} Hz mono signed 16-bit PCM; "
            "press Ctrl+C to stop",
            flush=True,
        )

    try:
        sock = connect(args.host, args.port)
        try:
            resample_state = None
            output_buffer = bytearray()
            while True:
                if ffmpeg_process is not None:
                    data = ffmpeg_process.stdout.read(CHUNK * 2 * CHANNELS)
                    if not data:
                        raise RuntimeError("FFmpeg DirectShow capture ended")
                    sock.sendall(data)
                else:
                    native_data = stream.read(native_chunk, exception_on_overflow=False)
                    converted, resample_state = pcm_to_output_rate(
                        native_data, native_rate, resample_state
                    )
                    output_buffer.extend(converted)
                    output_bytes = CHUNK * 2 * CHANNELS
                    while len(output_buffer) >= output_bytes:
                        sock.sendall(output_buffer[:output_bytes])
                        del output_buffer[:output_bytes]
        finally:
            sock.close()
    except KeyboardInterrupt:
        pass
    finally:
        if stream is not None:
            stream.stop_stream()
            stream.close()
        if ffmpeg_process is not None:
            ffmpeg_process.terminate()
            try:
                ffmpeg_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                ffmpeg_process.kill()
                ffmpeg_process.wait()
        audio.terminate()


if __name__ == "__main__":
    main()
