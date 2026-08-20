param(
    [ValidateSet('vm','board')][string]$Mode = 'vm',
    [string]$BoardHost = '192.168.77.2',
    [string]$BoardUser = 'root',
    [string]$MicDevice = 'auto',
    [switch]$NoMic,
    [switch]$NoGui,
    [switch]$NoBoardGui
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
function Start-EdgeProcess([string]$Name, [string]$File, [string[]]$Args) {
    $joined = $Args -join ' '
    Write-Host "START ${Name}: $File $joined"
    Start-Process -FilePath $File -ArgumentList $Args -WorkingDirectory $Root
}

if ($Mode -eq 'vm') {
    Start-EdgeProcess 'ASR service' 'python' @('pc\asr_tcp_service.py')
    if (-not $NoGui) { Start-EdgeProcess 'GUI' 'python' @('pc\edgeaudio_gui.py', '--host', '127.0.0.1', '--input-device', "$MicDevice") }
    if (-not $NoMic) {
        Start-EdgeProcess 'microphone sender' 'python' @('pc\mic_sender.py', '--host', '127.0.0.1', '--device', "$MicDevice")
    }
} else {
    Write-Host 'Board mode requires the board core to be deployed first. Checking SSH...'
    ssh -o ConnectTimeout=5 "$BoardUser@$BoardHost" 'uname -a && test -x /root/edgeaudio/bin/audio_receiver'
    if ($LASTEXITCODE -ne 0) { throw 'Board SSH or deployed binary check failed' }
    Start-EdgeProcess 'GUI' 'python' @('pc\edgeaudio_gui.py', '--host', $BoardHost, '--port', '5702', '--input-device', "$MicDevice")
    if (-not $NoMic) { Start-EdgeProcess 'microphone sender' 'python' @('pc\mic_sender.py', '--host', $BoardHost, '--device', "$MicDevice") }
    ssh "$BoardUser@$BoardHost" 'mkdir -p /root/edgeaudio/logs && export LD_LIBRARY_PATH=/root/edgeaudio/lib:$LD_LIBRARY_PATH && chmod +x /root/edgeaudio/bin/thermal_guard.sh && nohup /root/edgeaudio/bin/thermal_guard.sh -- /root/edgeaudio/bin/audio_receiver --labels /root/edgeaudio/models/yamnet_class_map.csv --yamnet-backend rknn --yamnet-model /root/edgeaudio/models/yamnet_3s.rknn --asr-backend cpu --asr-tokens /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/tokens.txt --asr-encoder /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/encoder-epoch-99-avg-1.int8.onnx --asr-decoder /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/decoder-epoch-99-avg-1.onnx --asr-joiner /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/joiner-epoch-99-avg-1.int8.onnx >/root/edgeaudio/logs/audio_receiver.log 2>&1 &'
    if (-not $NoBoardGui) {
        $boardGuiCommand = 'mkdir -p /root/edgeaudio/logs /root/edgeaudio/run; if test -f /root/edgeaudio/run/result_relay.pid; then kill "$(cat /root/edgeaudio/run/result_relay.pid)" 2>/dev/null || true; fi; export PYTHONUNBUFFERED=1; nohup python3 /root/edgeaudio/bin/edgeaudio_result_relay.py --upstream-port 5701 --listen-host 0.0.0.0 --listen-port 5702 >/root/edgeaudio/logs/result_relay.log 2>&1 </dev/null & echo $! > /root/edgeaudio/run/result_relay.pid; sleep 1; if test -f /root/edgeaudio/run/board_gui.pid; then kill "$(cat /root/edgeaudio/run/board_gui.pid)" 2>/dev/null || true; fi; export DISPLAY=:0 XAUTHORITY=/var/run/lightdm/root/:0; nohup python3 /root/edgeaudio/bin/edgeaudio_board_gui.py --host 127.0.0.1 --port 5702 >/root/edgeaudio/logs/board_gui.log 2>&1 </dev/null & echo $! > /root/edgeaudio/run/board_gui.pid'
        ssh "$BoardUser@$BoardHost" $boardGuiCommand
    }
}
