@echo off
setlocal
cd /d "%~dp0"
echo Starting the Monzero node. Keep this window open.
monzerod.exe --rpc-bind-ip 127.0.0.1 --zmq-rpc-bind-ip 127.0.0.1 --disable-rpc-ban --add-priority-node node.monzero.org:6174 --add-priority-node node2.monzero.org:6174 --no-igd
if errorlevel 1 (
  echo.
  echo The node stopped with an error. Check %%APPDATA%%\monzero\monzero.log
  pause
)
