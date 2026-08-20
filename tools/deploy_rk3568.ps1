param(
    [string]$BoardHost = '192.168.77.2',
    [string]$BoardUser = 'root',
    [string]$RemoteRoot = '/root/edgeaudio',
    [string]$SherpaOnnxRoot = ''
)
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SherpaOnnxRoot = if ($SherpaOnnxRoot) {
    (Resolve-Path $SherpaOnnxRoot).Path
} else {
    Join-Path $Root 'third_party\sherpa-onnx-build\install'
}
$Build = Join-Path $Root 'build'
if (-not (Test-Path (Join-Path $Build 'audio_receiver'))) {
    throw 'build/audio_receiver is missing. Build on Ubuntu with tools/build_board.sh first.'
}
$Package = Join-Path $env:TEMP 'edgeaudio-board-package'
if (Test-Path $Package) { Remove-Item -LiteralPath $Package -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $Package 'bin'), (Join-Path $Package 'models'), (Join-Path $Package 'lib') | Out-Null
Copy-Item (Join-Path $Build 'audio_receiver') (Join-Path $Package 'bin\audio_receiver')
Copy-Item (Join-Path $Root 'tools\thermal_guard.sh') (Join-Path $Package 'bin\thermal_guard.sh')
Copy-Item (Join-Path $Root 'models\yamnet_3s.rknn') (Join-Path $Package 'models\yamnet_3s.rknn')
Copy-Item (Join-Path $Root 'models\yamnet_class_map.csv') (Join-Path $Package 'models\yamnet_class_map.csv')
$asrSource = Join-Path $Root 'models\asr\sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23'
if (Test-Path $asrSource) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Package 'models\asr') | Out-Null
    Copy-Item $asrSource (Join-Path $Package 'models\asr') -Recurse
} else {
    Write-Warning 'ASR model directory is absent; run tools/download_asr_model.ps1 before board deployment.'
}
$rknnLib = Join-Path $Root 'deps\rknn_runtime_2.3.2\lib\aarch64\librknnrt.so'
if (Test-Path $rknnLib) { Copy-Item $rknnLib (Join-Path $Package 'lib\librknnrt.so') }
$sherpaLibDir = Join-Path $SherpaOnnxRoot 'lib'
$sherpaLibs = @(Get-ChildItem -Path (Join-Path $sherpaLibDir '*.so*') -File -ErrorAction SilentlyContinue)
if ($sherpaLibs.Count -gt 0) {
    foreach ($lib in $sherpaLibs) { Copy-Item $lib.FullName (Join-Path $Package 'lib') }
} else {
    Write-Warning "No sherpa-onnx shared libraries found under $sherpaLibDir; deploy CPU ASR only after supplying -SherpaOnnxRoot."
}
$Archive = Join-Path $env:TEMP 'edgeaudio-board-package.tar.gz'
if (Test-Path $Archive) { Remove-Item -LiteralPath $Archive -Force }
tar -czf $Archive -C $Package .
scp $Archive "${BoardUser}@${BoardHost}:/tmp/edgeaudio-board-package.tar.gz"
ssh "${BoardUser}@${BoardHost}" "mkdir -p $RemoteRoot && tar -xzf /tmp/edgeaudio-board-package.tar.gz -C $RemoteRoot && chmod +x $RemoteRoot/bin/audio_receiver"
Write-Host "Deployed C++ board package to ${BoardUser}@${BoardHost}:$RemoteRoot"

