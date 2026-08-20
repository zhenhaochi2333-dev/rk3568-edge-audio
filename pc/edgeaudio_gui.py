#!/usr/bin/env python3
"""Small Tk GUI for EdgeAudio newline-delimited JSON results."""

from __future__ import annotations

import argparse
import json
import queue
import socket
import threading
import tkinter as tk
from tkinter import ttk


class EdgeAudioGui:
    def __init__(self, host: str, port: int):
        self.host, self.port = host, port
        self.root = tk.Tk()
        self.root.title("RK3568 EdgeAudio")
        self.root.geometry("900x620")
        self.queue: queue.Queue[dict] = queue.Queue()
        self.sock: socket.socket | None = None
        self.stop_event = threading.Event()
        self.vars = {name: tk.StringVar(value="--") for name in (
            "connection", "mic", "audio", "sound", "yamnet", "asr", "partial",
            "final", "command", "yamnet_latency", "asr_latency", "rtf")}
        self.style = ttk.Style(self.root)
        self.style.configure("Connected.TLabel", foreground="#16803c")
        self.style.configure("Warning.TLabel", foreground="#b36b00")
        self.style.configure("Error.TLabel", foreground="#b42318")
        self.connection_label = None
        self.audio_bar = None
        self._build()
        threading.Thread(target=self._reader, daemon=True).start()
        self.root.after(100, self._poll)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self) -> None:
        frame = ttk.Frame(self.root, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)
        fields = [
            ("Connection", "connection"), ("Mic / VAD", "mic"), ("Audio level", "audio"),
            ("Stable sound event", "sound"), ("Sound backend", "yamnet"), ("ASR backend", "asr"),
            ("ASR partial", "partial"), ("ASR final", "final"), ("Command", "command"),
            ("YAMNet latency", "yamnet_latency"), ("ASR latency", "asr_latency"), ("ASR RTF", "rtf"),
        ]
        for row, (label, key) in enumerate(fields):
            ttk.Label(frame, text=label + ":", width=22).grid(row=row, column=0, sticky=tk.W, pady=3)
            if key == "connection":
                self.connection_label = ttk.Label(frame, textvariable=self.vars[key], style="Warning.TLabel")
                self.connection_label.grid(row=row, column=1, sticky=tk.W, pady=3)
            elif key == "audio":
                level = ttk.Frame(frame)
                level.grid(row=row, column=1, sticky=tk.EW, pady=3)
                ttk.Label(level, textvariable=self.vars[key], width=18).pack(side=tk.LEFT)
                self.audio_bar = ttk.Progressbar(level, orient=tk.HORIZONTAL, mode="determinate",
                                                 maximum=100, length=170)
                self.audio_bar.pack(side=tk.LEFT, padx=(8, 0))
            else:
                ttk.Label(frame, textvariable=self.vars[key]).grid(row=row, column=1, sticky=tk.W, pady=3)
        ttk.Label(frame, text="Recent events:").grid(row=0, column=2, sticky=tk.W, padx=(30, 0))
        self.history = tk.Listbox(frame, width=70, height=28)
        self.history.grid(row=1, column=2, rowspan=12, sticky=tk.NSEW, padx=(30, 0))
        frame.columnconfigure(2, weight=1)
        frame.rowconfigure(12, weight=1)

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
            self.vars["connection"].set(item["value"])
            if self.connection_label:
                self.connection_label.configure(
                    style="Connected.TLabel" if item["value"].startswith("CONNECTED") else "Warning.TLabel")
            self._history(item["value"])
        elif kind == "status":
            self.vars["yamnet"].set(item.get("yamnet_backend", "--"))
            self.vars["asr"].set(item.get("asr_backend", "--"))
            self.vars["mic"].set("SPEECH" if item.get("speech") else "SILENCE")
            self._audio_level(item.get("audio_rms", 0.0))
            if item.get("state") == "asr_error":
                self.vars["asr"].set("ERROR")
                self.vars["final"].set(item.get("error", "ASR error"))
                self._history("ERROR " + item.get("error", "ASR error"))
        elif kind == "vad":
            state = item.get("state", "--")
            self.vars["mic"].set(state)
            self._audio_level(item.get("rms", 0.0))
            self._history("VAD " + state)
        elif kind == "sound_event":
            topk = item.get("topk", [])
            best = topk[0] if topk else {}
            label = item.get("stable_event") or best.get("label", "--")
            score = best.get("score")
            self.vars["sound"].set(f"{label} ({float(score):.2f})" if score is not None else label)
            self.vars["yamnet"].set(item.get("backend", "--"))
            self.vars["yamnet_latency"].set(f"{item.get('inference_ms', 0):.2f} ms")
            self._history("SOUND " + ", ".join(x.get("label", "") for x in topk[:5]))
        elif kind == "asr":
            text = item.get("text", "")
            self.vars["asr"].set(item.get("backend", "--"))
            self.vars["asr_latency"].set(f"{item.get('latency_ms', 0):.2f} ms")
            self.vars["rtf"].set(str(item.get("rtf", "--")))
            if item.get("final"):
                self.vars["partial"].set("--")
                self.vars["final"].set(text or "--")
            else:
                self.vars["partial"].set(text or "--")
            command = item.get("command")
            if command:
                action = {
                    "START_MONITORING": "Monitoring Started",
                    "STOP_MONITORING": "Monitoring Stopped",
                    "QUERY_STATUS": "Status Queried",
                }.get(command, "Command Recognized")
                self.vars["command"].set(f"{command} | {action}")
                self._history(f"COMMAND {command} -> {action}")
            self._history(("FINAL " if item.get("final") else "PARTIAL ") + text)

    def _audio_level(self, rms) -> None:
        try:
            value = max(0.0, float(rms))
        except (TypeError, ValueError):
            value = 0.0
        percent = min(100.0, value / 0.08 * 100.0)
        self.vars["audio"].set(f"RMS {value:.3f} ({percent:.0f}%)")
        if self.audio_bar is not None:
            self.audio_bar["value"] = percent

    def _history(self, text: str) -> None:
        self.history.insert(0, text)
        if self.history.size() > 100:
            self.history.delete(100, tk.END)

    def close(self) -> None:
        self.stop_event.set()
        if self.sock:
            self.sock.close()
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5701)
    args = parser.parse_args()
    EdgeAudioGui(args.host, args.port).root.mainloop()


if __name__ == "__main__":
    main()
