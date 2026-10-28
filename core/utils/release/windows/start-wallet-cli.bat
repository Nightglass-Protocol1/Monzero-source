@echo off
setlocal
cd /d "%~dp0"
monzero-wallet-cli.exe --daemon-address 127.0.0.1:6175
pause
