#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
extra_args=()
if [[ -n "${SHERPA_ONNX_ROOT:-}" ]]; then
  extra_args+=("-DSHERPA_ONNX_ROOT=$SHERPA_ONNX_ROOT")
fi
cmake -S "$ROOT/rk3568" -B "$ROOT/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRKNN_ROOT="${RKNN_ROOT:-$ROOT/deps/rknn_runtime_2.3.2}" \
  "${extra_args[@]}"
if [[ -x "$ROOT/tools/thermal_guard.sh" ]]; then
  "$ROOT/tools/thermal_guard.sh" -- cmake --build "$ROOT/build" -j"$(nproc)"
else
  cmake --build "$ROOT/build" -j"$(nproc)"
fi

