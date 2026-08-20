param(
    [ValidateSet('vm','board')][string]$Mode = 'vm',
    [string]$BoardHost = '192.168.77.2',
    [string]$BoardUser = 'root',
    [int]$MicDevice = 6,
    [switch]$NoMic,
    [switch]$NoGui
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
    if (-not $NoGui) { Start-EdgeProcess 'GUI' 'python' @('pc\edgeaudio_gui.py', '--host', '127.0.0.1') }
    if (-not $NoMic) {
        Start-EdgeProcess 'microphone sender' 'python' @('pc\mic_sender.py', '--host', '127.0.0.1', '--device', "$MicDevice")
    }
} else {
    Write-Host 'Board mode requires the board core to be deployed first. Checking SSH...'
    ssh -o ConnectTimeout=5 "$BoardUser@$BoardHost" 'uname -a && test -x /root/edgeaudio/bin/audio_receiver'
    if ($LASTEXITCODE -ne 0) { throw 'Board SSH or deployed binary check failed' }
    Start-EdgeProcess 'GUI' 'python' @('pc\edgeaudio_gui.py', '--host', $BoardHost)
    if (-not $NoMic) { Start-EdgeProcess 'microphone sender' 'python' @('pc\mic_sender.py', '--host', $BoardHost, '--device', "$MicDevice") }
    ssh "$BoardUser@$BoardHost" 'mkdir -p /root/edgeaudio/logs && nohup /root/edgeaudio/bin/audio_receiver --labels /root/edgeaudio/models/yamnet_class_map.csv --yamnet-backend rknn --yamnet-model /root/edgeaudio/models/yamnet_3s.rknn --asr-backend cpu --asr-tokens /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/tokens.txt --asr-encoder /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/encoder-epoch-99-avg-1.int8.onnx --asr-decoder /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/decoder-epoch-99-avg-1.onnx --asr-joiner /root/edgeaudio/models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23/joiner-epoch-99-avg-1.int8.onnx >/root/edgeaudio/logs/audio_receiver.log 2>&1 &'
}

