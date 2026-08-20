$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$AsrDir = Join-Path $Root 'models\asr'
$Archive = Join-Path $AsrDir 'zipformer-zh-14M.tar.bz2'
New-Item -ItemType Directory -Force -Path $AsrDir | Out-Null
if (-not (Test-Path $Archive)) {
    Invoke-WebRequest `
        -Uri 'https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23.tar.bz2' `
        -OutFile $Archive
}
python -c "import tarfile; tarfile.open(r'$Archive','r:bz2').extractall(r'$AsrDir')"
Write-Host "ASR model ready under $AsrDir"
