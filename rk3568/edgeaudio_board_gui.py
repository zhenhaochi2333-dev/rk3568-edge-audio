#!/usr/bin/env python3
"""RK3568 local monitor for the EdgeAudio result stream.

This is a display-only process. It consumes the existing newline-delimited JSON
result stream and never opens the audio input or changes the board pipeline.
"""

from __future__ import print_function

import argparse
import json
import os
import socket
import threading

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk

SOUND_SCORE_THRESHOLD = 0.12
SOUND_STABLE_FRAMES = 3


class BoardGui(object):
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.stop_event = threading.Event()
        self.sock = None
        self.last_connection = None
        self.sound_candidate = None
        self.sound_candidate_count = 0
        self.last_stable_sound = "__quiet__"
        self.history_rows = []
        self.values = {}
        self.row_counts = {}

        self.window = Gtk.Window(title="RK3568 EdgeAudio · Board Monitor")
        self.window.set_default_size(1180, 700)
        self.window.set_position(Gtk.WindowPosition.CENTER)
        self.window.connect("destroy", self.close)
        self._install_css()

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        root.set_border_width(16)
        self.window.add(root)

        title = Gtk.Label()
        title.set_text("RK3568 EdgeAudio")
        title.get_style_context().add_class("title")
        title.set_xalign(0.0)
        root.pack_start(title, False, False, 0)

        subtitle = Gtk.Label(label="BOARD LOCAL MONITOR  ·  JSON result stream 127.0.0.1:%d" % port)
        subtitle.get_style_context().add_class("muted")
        subtitle.set_xalign(0.0)
        root.pack_start(subtitle, False, False, 0)

        body = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=14)
        root.pack_start(body, True, True, 0)

        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        left.set_size_request(570, -1)
        body.pack_start(left, True, True, 0)
        right = Gtk.Frame(label="IMPORTANT EVENTS · LAST 10")
        right.set_size_request(540, -1)
        body.pack_start(right, True, True, 0)

        self.status = self._make_section("CURRENT STATUS")
        left.pack_start(self.status[0], False, False, 0)
        self._add_row(self.status[1], "Core connection", "connection")
        self._add_row(self.status[1], "Input stream", "input")
        self._add_row(self.status[1], "Mic / VAD", "mic")
        self._add_level_row(self.status[1])
        self._add_row(self.status[1], "Monitoring", "monitoring", current=True)
        self._add_row(self.status[1], "Board temperature", "temperature")

        self.sound = self._make_section("SOUND EVENTS · YAMNET")
        left.pack_start(self.sound[0], False, False, 0)
        self._add_row(self.sound[1], "Current sound", "sound", current=True)
        self._add_row(self.sound[1], "Top-5 sounds", "topk")
        self._add_row(self.sound[1], "Backend", "yamnet")
        self._add_row(self.sound[1], "Latency", "yamnet_latency")

        self.speech = self._make_section("SPEECH & COMMAND")
        left.pack_start(self.speech[0], True, True, 0)
        self._add_row(self.speech[1], "ASR partial", "partial", muted=True)
        self._add_row(self.speech[1], "ASR final", "final", final=True)
        self._add_row(self.speech[1], "Command", "command")
        self._add_row(self.speech[1], "ASR backend", "asr")

        self.performance = self._make_section("PERFORMANCE")
        left.pack_start(self.performance[0], False, False, 0)
        self._add_row(self.performance[1], "YAMNet latency", "yamnet_latency")
        self._add_row(self.performance[1], "ASR latency", "asr_latency")
        self._add_row(self.performance[1], "ASR RTF", "rtf")

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.history = Gtk.ListBox()
        self.history.set_selection_mode(Gtk.SelectionMode.NONE)
        scroll.add(self.history)
        right.add(scroll)

        self.level_bar = None
        self._set_value("connection", "DISCONNECTED")
        self._set_value("input", "PC microphone → TCP PCM16")
        self._set_value("mic", "SILENCE")
        self._set_value("audio", "QUIET (0%)")
        self._set_value("monitoring", "ON")
        self._set_value("sound", "Listening / quiet")
        self._set_value("topk", "--")
        self._set_value("yamnet", "--")
        self._set_value("temperature", "--")
        self._set_value("partial", "--")
        self._set_value("final", "--")
        self._set_value("command", "--")
        self._set_value("asr", "--")
        self._set_value("yamnet_latency", "--")
        self._set_value("asr_latency", "--")
        self._set_value("rtf", "--")

        self.window.show_all()
        self._add_history("BOARD UI READY")
        GLib.timeout_add(2000, self._poll_temperature)
        threading.Thread(target=self._reader, daemon=True).start()

    def _install_css(self):
        css = Gtk.CssProvider()
        css.load_from_data(b"""
        .title { font-size: 24px; font-weight: bold; color: #173f8a; }
        .section-title { font-weight: bold; color: #444444; }
        .value { font-size: 14px; }
        .current { font-size: 19px; font-weight: bold; color: #1d4ed8; }
        .final { font-size: 17px; font-weight: bold; color: #111827; }
        .muted { color: #6b7280; }
        .ok { color: #16803c; }
        .warning { color: #b36b00; }
        .critical { color: #b42318; font-weight: bold; }
        """)
        Gtk.StyleContext.add_provider_for_screen(
            self.window.get_screen(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def _make_section(self, title):
        frame = Gtk.Frame(label=title)
        frame.set_border_width(8)
        label = frame.get_label_widget()
        if label:
            label.get_style_context().add_class("section-title")
        grid = Gtk.Grid(column_spacing=12, row_spacing=8)
        frame.add(grid)
        self.row_counts[id(grid)] = 0
        return frame, grid

    def _next_row(self, grid):
        key = id(grid)
        row = self.row_counts.get(key, 0)
        self.row_counts[key] = row + 1
        return row

    def _add_row(self, grid, label, key, current=False, muted=False, final=False):
        row = self._next_row(grid)
        name = Gtk.Label(label=label + ":")
        name.set_xalign(0.0)
        grid.attach(name, 0, row, 1, 1)
        value = Gtk.Label()
        value.set_xalign(0.0)
        value.set_selectable(True)
        value.get_style_context().add_class("value")
        if current:
            value.get_style_context().add_class("current")
        if muted:
            value.get_style_context().add_class("muted")
        if final:
            value.get_style_context().add_class("final")
        if key == "topk":
            value.set_line_wrap(True)
            value.set_max_width_chars(32)
        grid.attach(value, 1, row, 1, 1)
        grid.set_column_homogeneous(False)
        self.values[key] = value

    def _add_level_row(self, grid):
        row = self._next_row(grid)
        name = Gtk.Label(label="Audio level:")
        name.set_xalign(0.0)
        grid.attach(name, 0, row, 1, 1)
        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        label = Gtk.Label()
        label.set_xalign(0.0)
        self.values["audio"] = label
        box.pack_start(label, False, False, 0)
        self.level_bar = Gtk.ProgressBar()
        self.level_bar.set_show_text(False)
        self.level_bar.set_size_request(180, -1)
        box.pack_start(self.level_bar, False, False, 0)
        grid.attach(box, 1, row, 1, 1)

    def _set_value(self, key, value, css=None):
        label = self.values.get(key)
        if label is None:
            return
        label.set_text(str(value))
        if css:
            context = label.get_style_context()
            for name in ("ok", "warning", "critical"):
                context.remove_class(name)
            context.add_class(css)

    def _add_history(self, text):
        row = Gtk.Label(label=text)
        row.set_xalign(0.0)
        row.set_line_wrap(True)
        row.set_margin_top(3)
        row.set_margin_bottom(3)
        self.history.insert(row, 0)
        self.history_rows.insert(0, row)
        while len(self.history_rows) > 10:
            old = self.history_rows.pop()
            if old.get_parent() is not None:
                self.history.remove(old)
        self.history.show_all()

    def _audio_level(self, rms, speech=None):
        try:
            value = max(0.0, float(rms)) if rms is not None else 0.0
        except (TypeError, ValueError):
            value = 0.0
        if value <= 0.0 and speech is not None:
            percent = 65.0 if speech else 0.0
            text = "SPEECH activity (%.0f%%)" % percent if speech else "QUIET (0%)"
        else:
            percent = min(100.0, value / 0.08 * 100.0)
            text = "RMS %.3f (%.0f%%)" % (value, percent)
        self._set_value("audio", text)
        if self.level_bar:
            self.level_bar.set_fraction(percent / 100.0)

    def _handle(self, item):
        kind = item.get("type")
        if kind == "_connection":
            value = item.get("value", "DISCONNECTED")
            if value == self.last_connection:
                return
            self.last_connection = value
            self._set_value("connection", value, "ok" if value.startswith("CONNECTED") else "warning")
            self._add_history("🔌 " + value)
        elif kind == "status":
            speech = bool(item.get("speech"))
            self._set_value("mic", "SPEECH" if speech else "SILENCE")
            self._audio_level(item.get("audio_rms"), speech)
            if "monitoring" in item:
                self._set_value("monitoring", "ON" if item["monitoring"] else "OFF")
            self._set_value("asr", item.get("asr_backend", "--"))
        elif kind == "vad":
            state = item.get("state", "--")
            self._set_value("mic", state)
            self._audio_level(item.get("rms"), state == "speech_start")
            if state == "speech_start":
                self._set_value("partial", "Listening...")
                self._add_history("🎤 SPEECH START")
            elif state == "speech_end":
                self._add_history("🎤 SPEECH END")
        elif kind == "sound_event":
            topk = item.get("topk", [])
            best = topk[0] if topk else {}
            topk_lines = []
            for entry in topk[:5]:
                try:
                    topk_lines.append("%s: %.2f" % (entry.get("label", "--"), float(entry.get("score", 0.0))))
                except (AttributeError, TypeError, ValueError):
                    continue
            self._set_value("topk", "\n".join(topk_lines) or "--")
            label = item.get("stable_event") or best.get("label", "--")
            try:
                score = float(best.get("score", 0.0))
            except (TypeError, ValueError):
                score = 0.0
            self._set_value("yamnet", item.get("backend", "--"))
            self._set_value("yamnet_latency", "%.2f ms" % float(item.get("inference_ms", 0)))
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
                    self._set_value("sound", "Listening / quiet")
                    self.last_stable_sound = "__quiet__"
                return
            if candidate != self.last_stable_sound:
                self._set_value("sound", "%s  (%.2f)" % (label.upper(), score))
                self.last_stable_sound = candidate
                self._add_history("🔊 SOUND | %s %.2f" % (label, score))
            else:
                self._set_value("sound", "%s  (%.2f)" % (label.upper(), score))
        elif kind == "asr":
            text = item.get("text", "")
            if "monitoring" in item:
                self._set_value("monitoring", "ON" if item["monitoring"] else "OFF")
            self._set_value("asr", item.get("backend", "--"))
            self._set_value("asr_latency", "%.2f ms" % float(item.get("latency_ms", 0)))
            self._set_value("rtf", item.get("rtf", "--"))
            if item.get("final"):
                final_text = text or "No text recognized"
                self._set_value("partial", "--")
                self._set_value("final", final_text)
                self._add_history("🎤 ASR | " + final_text)
            else:
                self._set_value("partial", text or "Listening...")
            command = item.get("command")
            if command:
                if command == "START_MONITORING":
                    self._set_value("monitoring", "ON")
                    self._set_value("sound", "Listening / quiet")
                    self._set_value("topk", "--")
                elif command == "STOP_MONITORING":
                    self._set_value("monitoring", "OFF")
                    self._set_value("sound", "Monitoring stopped")
                    self._set_value("topk", "--")
                self._set_value("command", command)
                self._add_history("⚙ COMMAND | " + command)

    def _reader(self):
        while not self.stop_event.is_set():
            try:
                sock = socket.create_connection((self.host, self.port), timeout=5)
                # Silence is valid: wait indefinitely for the next result line
                # instead of treating five seconds without JSON as a disconnect.
                sock.settimeout(None)
                self.sock = sock
                GLib.idle_add(self._handle, {"type": "_connection", "value": "CONNECTED %s:%d" % (self.host, self.port)})
                with sock:
                    file_obj = sock.makefile("r", encoding="utf-8", errors="replace")
                    for line in file_obj:
                        if self.stop_event.is_set():
                            break
                        try:
                            item = json.loads(line)
                        except ValueError:
                            continue
                        GLib.idle_add(self._handle, item)
                GLib.idle_add(self._handle, {"type": "_connection", "value": "DISCONNECTED"})
            except OSError:
                GLib.idle_add(self._handle, {"type": "_connection", "value": "DISCONNECTED"})
                self.stop_event.wait(1.0)

    def _poll_temperature(self):
        if self.stop_event.is_set():
            return False
        values = []
        for zone in ("thermal_zone0", "thermal_zone1"):
            path = "/sys/class/thermal/%s/temp" % zone
            try:
                with open(path, "r") as stream:
                    values.append(int(stream.read().strip()) / 1000.0)
            except (IOError, ValueError):
                pass
        if values:
            highest = max(values)
            css = "critical" if highest >= 78.0 else "warning" if highest >= 70.0 else "ok"
            self._set_value("temperature", "%.1f °C (max)" % highest, css)
        else:
            self._set_value("temperature", "unavailable", "warning")
        return True

    def close(self, *_args):
        self.stop_event.set()
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        Gtk.main_quit()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5702)
    args = parser.parse_args()
    BoardGui(args.host, args.port)
    Gtk.main()


if __name__ == "__main__":
    main()
