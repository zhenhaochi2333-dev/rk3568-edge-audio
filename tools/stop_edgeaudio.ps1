param(
    [string]$BoardHost = '192.168.77.2',
    [string]$BoardUser = 'root',
    [switch]$NoBoard
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$escaped = [regex]::Escape($Root)
$processes = Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.CommandLine -match $escaped -and (
        $_.CommandLine -match 'asr_tcp_service\.py' -or
        $_.CommandLine -match 'edgeaudio_gui\.py' -or
        $_.CommandLine -match 'mic_sender\.py' -or
        $_.CommandLine -match 'wav_sender\.py')
}
foreach ($process in $processes) {
    Write-Host "STOP PID $($process.ProcessId) $($process.Name)"
    Stop-Process -Id $process.ProcessId -Force
}
if (-not $processes) { Write-Host 'No EdgeAudio PC processes found.' }
if (-not $NoBoard) {
    ssh -o ConnectTimeout=5 "$BoardUser@$BoardHost" 'for name in board_gui relay; do file=/root/edgeaudio/run/${name}.pid; if test -f "$file"; then kill "$(cat "$file")" 2>/dev/null || true; rm -f "$file"; echo ${name}_STOPPED; fi; done'
}
