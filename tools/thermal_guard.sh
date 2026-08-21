#!/usr/bin/env bash
# Pause one EdgeAudio child process when the SoC temperature is too high.
set -euo pipefail

pause_c="${EDGEAUDIO_THERMAL_PAUSE_C:-78}"
resume_c="${EDGEAUDIO_THERMAL_RESUME_C:-68}"
interval_s="${EDGEAUDIO_THERMAL_POLL_S:-5}"
thermal_file="${EDGEAUDIO_THERMAL_FILE:-}"

if [[ -z "$thermal_file" ]]; then
  for candidate in /sys/class/thermal/thermal_zone*/temp; do
    [[ -r "$candidate" ]] || continue
    type_file="${candidate%/temp}/type"
    type="$(cat "$type_file" 2>/dev/null || true)"
    if [[ "$type" == *soc* || "$type" == *cpu* ]]; then
      thermal_file="$candidate"
      break
    fi
  done
fi

read_celsius() {
  local raw
  raw="$(cat "$thermal_file" 2>/dev/null || true)"
  if [[ "$raw" =~ ^[0-9]+$ ]]; then
    awk -v value="$raw" 'BEGIN { printf "%.1f\n", value / 1000.0 }'
  else
    printf '%s\n' '-1'
  fi
}

if [[ "$#" -lt 2 || "$1" != "--" ]]; then
  echo "usage: $0 -- command [args...]" >&2
  exit 2
fi
shift

if [[ -z "$thermal_file" ]]; then
  echo 'EDGEAUDIO_THERMAL=UNAVAILABLE; running without pause guard' >&2
  exec "$@"
fi

setsid "$@" &
child="$!"
paused=0
cleanup() {
  if [[ "$paused" -eq 1 ]]; then
    kill -CONT -- "-$child" 2>/dev/null || kill -CONT "$child" 2>/dev/null || true
  fi
}
trap 'cleanup; exit 143' INT TERM
while kill -0 "$child" 2>/dev/null; do
  temp="$(read_celsius)"
  if [[ "$temp" != '-1' ]]; then
    if [[ "$paused" -eq 0 ]] && awk -v t="$temp" -v limit="$pause_c" 'BEGIN { exit !(t >= limit) }'; then
      echo "EDGEAUDIO_THERMAL_PAUSE temp=${temp}C threshold=${pause_c}C" >&2
      kill -STOP -- "-$child" 2>/dev/null || kill -STOP "$child" 2>/dev/null || true
      paused=1
    elif [[ "$paused" -eq 1 ]] && awk -v t="$temp" -v limit="$resume_c" 'BEGIN { exit !(t <= limit) }'; then
      echo "EDGEAUDIO_THERMAL_RESUME temp=${temp}C threshold=${resume_c}C" >&2
      kill -CONT -- "-$child" 2>/dev/null || kill -CONT "$child" 2>/dev/null || true
      paused=0
    fi
  fi
  sleep "$interval_s"
done
wait "$child"

