param(
    [string]$BoardHost = '192.168.77.2',
    [string]$BoardUser = 'root',
    [string]$RemoteRoot = '/root/edgeaudio'
)
$ErrorActionPreference = 'Stop'

$checks = @(
    "uname -m",
    "test -x $RemoteRoot/bin/audio_receiver && echo binary=PASS",
    "test -f $RemoteRoot/models/yamnet_3s.rknn && echo yamnet_model=PASS",
    "test -f $RemoteRoot/models/yamnet_class_map.csv && echo labels=PASS",
    "test -f $RemoteRoot/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/tokens.txt && echo asr_model=PASS || echo asr_model=TO_VERIFY",
    "test -f $RemoteRoot/lib/librknnrt.so && echo rknn_runtime=PASS",
    "test -f $RemoteRoot/lib/libsherpa-onnx-c-api.so && echo sherpa_c_api=PASS || echo sherpa_c_api=TO_VERIFY",
    "test -f $RemoteRoot/lib/libonnxruntime.so && echo onnxruntime=PASS || echo onnxruntime=TO_VERIFY",
    "test -x $RemoteRoot/bin/thermal_guard.sh && echo thermal_guard=PASS || echo thermal_guard=TO_VERIFY",
    "test -f $RemoteRoot/bin/edgeaudio_board_gui.py && echo board_gui=PASS || echo board_gui=TO_VERIFY",
    "test -f $RemoteRoot/bin/edgeaudio_result_relay.py && echo result_relay=PASS || echo result_relay=TO_VERIFY",
    'for z in /sys/class/thermal/thermal_zone*/temp; do test -r "$z" && echo "$z=$(cat "$z")"; done',
    "ldd $RemoteRoot/bin/audio_receiver || true",
    "test -c /dev/rknpu0 && echo rknpu_device=PASS || echo rknpu_device=TO_VERIFY",
    "free -h"
)
foreach ($check in $checks) {
    Write-Host "CHECK $check"
    ssh -o ConnectTimeout=5 "${BoardUser}@${BoardHost}" $check
    if ($LASTEXITCODE -ne 0) { Write-Warning "failed: $check" }
}
Write-Host 'Dynamic acceptance remains for tomorrow: YAMNet RKNN latency, ASR RKNN/CPU RTF, live microphone, GUI reconnect.'
