"""Shared Windows input-device discovery and selection for EdgeAudio."""

from __future__ import annotations

from dataclasses import dataclass

import pyaudio


PREFERRED_NAME_TOKENS = ("K30S", "K03S")
_HOST_API_PRIORITY = ("WASAPI", "WDM-KS", "DIRECTSOUND", "MME")


@dataclass(frozen=True)
class InputDevice:
    index: int
    name: str
    host_api: str
    max_input_channels: int
    sample_rate: float

    @property
    def display_name(self) -> str:
        return f"{self.name} (index {self.index}, {self.host_api})"


def list_input_devices(audio: pyaudio.PyAudio) -> list[InputDevice]:
    devices: list[InputDevice] = []
    for index in range(audio.get_device_count()):
        info = audio.get_device_info_by_index(index)
        channels = int(info.get("maxInputChannels", 0))
        if channels <= 0:
            continue
        host_info = audio.get_host_api_info_by_index(int(info.get("hostApi", 0)))
        devices.append(InputDevice(
            index=index,
            name=str(info.get("name", "<unknown>")),
            host_api=str(host_info.get("name", "unknown")),
            max_input_channels=channels,
            sample_rate=float(info.get("defaultSampleRate", 0.0)),
        ))
    return devices


def _host_rank(device: InputDevice) -> int:
    name = device.host_api.upper()
    for rank, token in enumerate(_HOST_API_PRIORITY):
        if token in name:
            return rank
    return len(_HOST_API_PRIORITY)


def _supports_pcm16_rate(audio: pyaudio.PyAudio, device: InputDevice,
                         rate: int = 16000, channels: int = 1) -> bool:
    try:
        audio.is_format_supported(
            rate,
            input_device=device.index,
            input_channels=channels,
            input_format=pyaudio.paInt16,
        )
        return True
    except (OSError, ValueError):
        return False


def select_input_device(audio: pyaudio.PyAudio, requested: str = "auto",
                        rate: int = 16000, channels: int = 1) -> InputDevice:
    devices = list_input_devices(audio)
    if not devices:
        raise RuntimeError("no Windows input device is available")

    if requested.lower() != "auto":
        try:
            index = int(requested)
        except ValueError as exc:
            raise ValueError(f"invalid --device value: {requested}") from exc
        for device in devices:
            if device.index == index:
                return device
        raise RuntimeError(f"input device index {index} is not available")

    preferred = [
        device for device in devices
        if any(token in device.name.upper().replace(" ", "") for token in PREFERRED_NAME_TOKENS)
        and _supports_pcm16_rate(audio, device, rate, channels)
    ]
    if preferred:
        return min(preferred, key=lambda device: (_host_rank(device), device.index))

    try:
        default_info = audio.get_default_input_device_info()
        default_index = int(default_info["index"])
        for device in devices:
            if device.index == default_index:
                return device
    except (OSError, KeyError, ValueError):
        pass
    return devices[0]
