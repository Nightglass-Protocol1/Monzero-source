@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: start-mining.bat YOUR_MONZERO_ADDRESS [threads]
  exit /b 1
)
set "MONZERO_ADDRESS=%~1"
set "MONZERO_THREADS=%~2"
set "MONZERO_PACKAGE_DIR=%~dp0"
if "%MONZERO_THREADS%"=="" set "MONZERO_THREADS=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; $info=Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:6175/get_info' -ContentType 'application/json' -Body '{}' -TimeoutSec 5; if (-not $info.synchronized) { throw 'Node is not synchronized' }; $body=@{miner_address=$env:MONZERO_ADDRESS;threads_count=[int]$env:MONZERO_THREADS;do_background_mining=$false;ignore_battery=$true} | ConvertTo-Json -Compress; $result=Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:6175/start_mining' -ContentType 'application/json' -Body $body -TimeoutSec 10; if ($result.status -ne 'OK') { throw 'Mining failed' }; Write-Host 'Mining started'"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$token=Join-Path $env:APPDATA 'Monzero\miner-stats-token.txt'; $reporter=Join-Path $env:MONZERO_PACKAGE_DIR 'monzero-miner-reporter.ps1'; if (Test-Path $token) { Start-Process powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $reporter + '"')); Write-Host 'Anonymous website reporting enabled' } else { Write-Host 'Optional website reporter token is not installed; mining is still active' }"
