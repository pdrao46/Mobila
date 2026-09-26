# Baixa os componentes de runtime do MOBILADOR para third_party/
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$tp = Join-Path $root "third_party"
New-Item -ItemType Directory -Force $tp | Out-Null

$ver = "3.1"
$server = Join-Path $tp "scrcpy-server"
if (-not (Test-Path $server)) {
  Write-Host "Baixando scrcpy-server v$ver..."
  Invoke-WebRequest "https://github.com/Genymobile/scrcpy/releases/download/v$ver/scrcpy-server-v$ver" -OutFile $server
}
$pt = Join-Path $tp "platform-tools"
if (-not (Test-Path (Join-Path $pt "adb.exe"))) {
  Write-Host "Baixando Android platform-tools..."
  $zip = Join-Path $env:TEMP "platform-tools.zip"
  Invoke-WebRequest "https://dl.google.com/android/repository/platform-tools-latest-windows.zip" -OutFile $zip
  Expand-Archive $zip -DestinationPath $tp -Force
}
Write-Host "OK: $tp"
