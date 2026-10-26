@echo off
rem Build with MinGW-w64 (g++ and windres on PATH, e.g. w64devkit or MSYS2 ucrt64).
setlocal
cd /d "%~dp0"
if not exist build mkdir build
windres app.rc -O coff -o build\app.res.o || exit /b 1
g++ -std=c++17 -O2 -Wall -municode -mwindows -static -DUNICODE -D_UNICODE main.cpp ftp_server.cpp build\app.res.o -o build\ScanReceiver.exe -lws2_32 -lcomctl32 -lshell32 -lole32 -luuid -ldwmapi -lwinmm -ladvapi32 -lgdiplus -lmsimg32 || exit /b 1
echo.
echo Built: %~dp0build\ScanReceiver.exe
