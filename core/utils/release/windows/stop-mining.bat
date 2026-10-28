@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; $result=Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:6175/stop_mining' -ContentType 'application/json' -Body '{}' -TimeoutSec 120; if ($result.status -ne 'OK') { throw 'Unable to stop mining' }; Write-Host 'Mining stopped'"
