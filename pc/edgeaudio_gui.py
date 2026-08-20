#!/usr/bin/env python3
"""Small Tk GUI for EdgeAudio newline-delimited JSON results."""

from __future__ import annotations

import argparse
import json
import queue
import socket
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk

import pyaudio

from audio_device import select_input_device

SOUND_SCORE_THRESHOLD = 0.12
SOUND_STABLE_FRAMES = 3

class EdgeAudioGui:
    def __init__(self, host: str, port: int, input_device: str = "auto"):
        self.host, self.port = host, port
        self.input_device_request = input_device
        self.root = tk.Tk()
        self.root.title("RK3568 EdgeAudio")
        self.root.geometry("1080x650")
        self.queue: queue.Queue[dict] = queue.Queue()
        self.sock: socket.socket | None = None
        self.stop_event = threading.Event()
        self.vars = {name: tk.StringVar(value="--") for name in (
            "connection", "audio_input", "mic", "audio", "monitoring", "sound", "topk", "yamnet",
            "asr", "partial", "final", "command", "yamnet_latency", "asr_latency", "rtf")}
        self.vars["sound"].set("Listening / quiet")
        self.vars["monitoring"].set("ON")
        self.vars["topk"].set("--")
        self.vars["audio_input"].set(self._resolve_input_device())
        self.last_connection = None
        self.sound_candidate = None
        self.sound_candidate_count = 0
        self.last_stable_sound = "__quiet__"
        self.style = ttk.Style(self.root)
        self.style.configure("Connected.TLabel", foreground="#16803c")
        self.style.configure("Warning.TLabel", foreground="#b36b00")
        self.style.configure("Error.TLabel", foreground="#b42318")
        self.style.configure("Current.TLabel", foreground="#1d4ed8", font=("Segoe UI", 14, "bold"))
        self.style.configure("Partial.TLabel", foreground="#6b7280")
        self.style.configure("Final.TLabel", foreground="#111827", font=("Segoe UI", 12, "bold"))
        self.connection_label = None
        self.audio_bar = None
        self._build()
        threading.Thread(target=self._reader, daemon=True).start()
        self.root.after(100, self._poll)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _resolve_input_device(self) -> str:
        try:
            audio = pyaudio.PyAudio()
            try:
                return select_input_device(audio, self.input_device_request).display_name
            finally:
                audio.terminate()
        except (OSError, RuntimeError, ValueError) as exc:
            return f"Unavailable ({exc})"

    def _build(self) -> None:
        frame = ttk.Frame(self.root, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)
        frame.columnconfigure(0, weight=1)
        frame.columnconfigure(1, weight=1)
        frame.columnconfigure(2, weight=1)
        frame.rowconfigure(0, weight=1)
        frame.rowconfigure(1, weight=0)

        sound = ttk.LabelFrame(frame, text="SOUND EVENTS · YAMNET", padding=10)
        sound.grid(row=0, column=0, sticky=tk.NSEW, padx=(0, 8), pady=(0, 8))
        speech = ttk.LabelFrame(frame, text="SPEECH & COMMAND", padding=10)
        speech.grid(row=0, column=1, sticky=tk.NSEW, padx=8, pady=(0, 8))
        perf = ttk.LabelFrame(frame, text="PERFORMANCE", padding=10)
        perf.grid(row=1, column=0, columnspan=3, sticky=tk.EW, pady=(0, 8))
        history_frame = ttk.LabelFrame(frame, text="IMPORTANT EVENTS · LAST 10", padding=8)
        history_frame.grid(row=0, column=2, sticky=tk.NSEW, padx=(8, 0), pady=(0, 8))
        history_frame.rowconfigure(0, weight=1)
        history_frame.columnconfigure(0, weight=1)

        def add_field(parent, row, label, key, style=""):
            ttk.Label(parent, text=label + ":", width=18).grid(
                row=row, column=0, sticky=tk.W, pady=4)
            if key == "audio":
                level = ttk.Frame(parent)
                level.grid(row=row, column=1, sticky=tk.EW, pady=4)
                ttk.Label(level, textvariable=self.vars[key], width=18).pack(side=tk.LEFT)
                self.audio_bar = ttk.Progressbar(level, orient=tk.HORIZONTAL, mode="determinate",
                                                 maximum=100, length=170)
                self.audio_bar.pack(side=tk.LEFT, padx=(8, 0))
                return
            if key == "topk":
                value = ttk.Label(parent, textvariable=self.vars[key], justify=tk.LEFT,
                                  anchor=tk.W, wraplength=250)
                value.grid(row=row, column=1, sticky=tk.NW, pady=4)
                return
            value = ttk.Label(parent, textvariable=self.vars[key], style=style)
            value.grid(row=row, column=1, sticky=tk.W, pady=4)
            if key == "connection":
                self.connection_label = value

        for row, (label, key, style) in enumerate([
            ("Monitoring", "monitoring", "Current.TLabel"),
            ("Current sound event", "sound", "Current.TLabel"),
            ("Top-5 sounds", "topk", ""),
            ("Sound backend", "yamnet", ""),
        ]):
            add_field(sound, row, label, key, style)
        for row, (label, key, style) in enumerate([
            ("Connection", "connection", "Warning.TLabel"),
            ("Audio input", "audio_input", ""),
            ("Mic / VAD", "mic", ""),
            ("Audio level", "audio", ""),
            ("ASR partial", "partial", "Partial.TLabel"),
            ("ASR final", "final", "Final.TLabel"),
            ("Command", "command", ""),
            ("ASR backend", "asr", ""),
        ]):
            add_field(speech, row, label, key, style)
        for row, (label, key, style) in enumerate([
            ("YAMNet latency", "yamnet_latency", ""),
            ("ASR latency", "asr_latency", ""),
            ("ASR RTF", "rtf", ""),
        ]):
            add_field(perf, row, label, key, style)
        self.history = tk.Listbox(history_frame, width=42, height=24, activestyle="none")
        self.history.grid(row=0, column=0, sticky=tk.NSEW)

    def _reader(self) -> None:
        while not self.stop_event.is_set():
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=5)
                self.sock.settimeout(None)
                self.queue.put({"type": "_connection", "value": f"CONNECTED {self.host}:{self.port}"})
                with self.sock.makefile("r", encoding="utf-8") as stream:
                    for line in stream:
                        if line.strip():
                            self.queue.put(json.loads(line))
                if not self.stop_event.is_set():
                    self.queue.put({"type": "_connection", "value": "RECONNECTING (server closed)"})
                    self.stop_event.wait(1.0)
            except (OSError, json.JSONDecodeError) as exc:
                self.queue.put({"type": "_connection", "value": f"RECONNECTING ({exc})"})
                self.stop_event.wait(1.0)

    def _poll(self) -> None:
        try:
            while True:
                self._handle(self.queue.get_nowait())
        except queue.Empty:
            pass
        if not self.stop_event.is_set():
            self.root.after(100, self._poll)

    def _handle(self, item: dict) -> None:
        kind = item.get("type")
        if kind == "_connection":
            value = item["value"]
            if value == self.last_connection:
                return
            self.last_connection = value
            self.vars["connection"].set(value)
            if self.connection_label:
                self.connection_label.configure(
                    style="Connected.TLabel" if value.startswith("CONNECTED") else "Warning.TLabel")
            self._history("🔌 " + value)
        elif kind == "status":
            self.vars["yamnet"].set(item.get("yamnet_backend", "--"))
            self.vars["asr"].set(item.get("asr_backend", "--"))
            if "monitoring" in item:
                self.vars["monitoring"].set("ON" if item["monitoring"] else "OFF")
            speech = bool(item.get("speech"))
            self.vars["mic"].set("SPEECH" if speech else "SILENCE")
            self._audio_level(item.get("audio_rms"), speech=speech)
            if item.get("state") == "asr_error":
                self.vars["asr"].set("ERROR")
                self.vars["final"].set(item.get("error", "ASR error"))
                self._history("ERROR " + item.get("error", "ASR error"))
        elif kind == "vad":
            state = item.get("state", "--")
            self.vars["mic"].set(state)
            self._audio_level(item.get("rms"), speech=state == "speech_start")
            if state == "speech_start":
                self.vars["partial"].set("Listening...")
                self._history("🎤 SPEECH START")
            elif state == "speech_end":
                self._history("🎤 SPEECH END")
        elif kind == "sound_event":
            topk = item.get("topk", [])
            best = topk[0] if topk else {}
            topk_lines = []
            for entry in topk[:5]:
                try:
                    topk_lines.append(f"{entry.get('label', '--')}: {float(entry.get('score', 0.0)):.2f}")
                except (AttributeError, TypeError, ValueError):
                    continue
            self.vars["topk"].set("\n".join(topk_lines) or "--")
            label = item.get("stable_event") or best.get("label", "--")
            try:
                score = float(best.get("score", 0.0))
            except (TypeError, ValueError):
                score = 0.0
            self.vars["yamnet"].set(item.get("backend", "--"))
            self.vars["yamnet_latency"].set(f"{item.get('inference_ms', 0):.2f} ms")
            quiet = not label or label.lower() == "silence" or score < SOUND_SCORE_THRESHOLD
            candidate = "__quiet__" if quiet else label
            if candidate == self.sound_candidate:
                self.sound_candidate_count += 1
            else:
                self.sound_candidate = candidate
                self.sound_candidate_count = 1
            if self.sound_candidate_count < SOUND_STABLE_FRAMES:
                return
            if candidate == "__quiet__":
                if self.last_stable_sound != "__quiet__":
                    self.vars["sound"].set("Listening / quiet")
                    self.last_stable_sound = "__quiet__"
                return
            if candidate != self.last_stable_sound:
                self.vars["sound"].set(f"{label.upper()}  ({score:.2f})")
                self.last_stable_sound = candidate
                self._history(f"🔊 SOUND | {label} {score:.2f}")
            else:
                self.vars["sound"].set(f"{label.upper()}  ({score:.2f})")
        elif kind == "asr":
            text = item.get("text", "")
            if "monitoring" in item:
                self.vars["monitoring"].set("ON" if item["monitoring"] else "OFF")
            self.vars["asr"].set(item.get("backend", "--"))
            self.vars["asr_latency"].set(f"{item.get('latency_ms', 0):.2f} ms")
            self.vars["rtf"].set(str(item.get("rtf", "--")))
            if item.get("final"):
                self.vars["partial"].set("--")
                final_text = text or "No text recognized"
                self.vars["final"].set(final_text)
                if text:
                    self._history("🎤 ASR | " + text)
                else:
                    self._history("🎤 ASR | No text recognized")
            else:
                self.vars["partial"].set(text or "Listening...")
            command = item.get("command")
            if command:
                if command == "START_MONITORING":
                    self.vars["monitoring"].set("ON")
                    self.vars["sound"].set("Listening / quiet")
                    self.vars["topk"].set("--")
                elif command == "STOP_MONITORING":
                    self.vars["monitoring"].set("OFF")
                    self.vars["sound"].set("Monitoring stopped")
                    self.vars["topk"].set("--")
                action = {
                    "START_MONITORING": "Monitoring Started",
                    "STOP_MONITORING": "Monitoring Stopped",
                    "QUERY_STATUS": "Status Queried",
                }.get(command, "Command Recognized")
                self.vars["command"].set(f"{command} | {action}")
                self._history(f"⚙ COMMAND | {command}")

    def _audio_level(self, rms, speech: bool | None = None) -> None:
        try:
            value = max(0.0, float(rms)) if rms is not None else 0.0
        except (TypeError, ValueError):
            value = 0.0
        if value <= 0.0 and speech is not None:
            # Older board binaries expose speech state but not the optional RMS field.
            # Show activity without pretending it is a measured amplitude.
            percent = 65.0 if speech else 0.0
            label = "SPEECH activity" if speech else "QUIET"
            self.vars["audio"].set(f"{label} ({percent:.0f}%)")
        else:
            percent = min(100.0, value / 0.08 * 100.0)
            self.vars["audio"].set(f"RMS {value:.3f} ({percent:.0f}%)")
        if self.audio_bar is not None:
            self.audio_bar["value"] = percent

    def _history(self, text: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.history.insert(0, f"{timestamp}  {text}")
        if self.history.size() > 10:
            self.history.delete(10, tk.END)

    def close(self) -> None:
        self.stop_event.set()
        if self.sock:
            self.sock.close()
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5701)
    parser.add_argument("--input-device", default="auto",
                        help="input device index, or auto to prefer K30S/K03S")
    args = parser.parse_args()
    EdgeAudioGui(args.host, args.port, args.input_device).root.mainloop()


if __name__ == "__main__":
    main()
