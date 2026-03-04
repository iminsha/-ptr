@echo off
setlocal

if "%~1"=="" (
  echo Usage: run_server.bat COMx [baudrate]
  exit /b 1
)

set PORT=%~1
set BAUD=%~2
if "%BAUD%"=="" set BAUD=115200

python src\serial_tcp_server.py --serial-port %PORT% --baudrate %BAUD% --bind-port 8888

 e:\anaconda\envs\dl\python.exe D:\qianrushu\mySmart\tcp_seri\src\serial_tcp_bridge.py --serial-port COM3 --baudrate 4800 --bind-port 8888