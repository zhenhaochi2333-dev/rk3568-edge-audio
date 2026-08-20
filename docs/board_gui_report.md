# RK3568 Board UI Report

## Result

The RK3568 now has a local GTK3 monitor that runs on the board's existing Xorg display:

- Display: `DISPLAY=:0`
- Resolution: `1280x800`
- Window: `RK3568 EdgeAudio · Board Monitor`
- Result source: local relay `127.0.0.1:5702`, fed by the formal `5701` JSON stream
- Runtime: board Python 3 + PyGObject/GTK3

## What it shows

- Core connection and input stream state
- Mic/VAD state and audio activity bar
- Stable sound event and YAMNet backend/latency
- ASR partial/final text and command
- ASR backend, latency and RTF
- Board temperature from `/sys/class/thermal/thermal_zone*/temp`
- Important event history limited to 10 entries

## Architecture boundary

The board UI is display-only. It does not open the microphone, run another model, modify the TCP protocol, or change `AudioReceiver`, `AudioRingBuffer`, VAD, YAMNet, ASR, or Command Parser.

## Verification

- Board script deployed to `/root/edgeaudio/bin/edgeaudio_board_gui.py`: PASS
- GTK process running with a board window: PASS
- Window size: `1180x700`: PASS
- UI connected to local relay result port `5702`: PASS
- Silent-period reconnect test: PASS; the board UI waits indefinitely for the next JSON line
- Board temperature during verification: approximately `38.3 °C` / `33.8 °C`: PASS

## Start/stop integration

`tools/start_edgeaudio.ps1 -Mode board` now starts the result relay and board monitor by default. Use `-NoBoardGui` to disable both. `tools/stop_edgeaudio.ps1` stops only the PID files recorded for the relay and board monitor on the board, in addition to the scoped EdgeAudio PC processes.
