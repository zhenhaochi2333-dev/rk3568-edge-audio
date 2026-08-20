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

