@echo off
rem Build with Visual Studio (MSVC). Run from any prompt; vcvars is located automatically.
setlocal
cd /d "%~dp0"
where cl >nul 2>nul && goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto nomsvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto nomsvc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || goto nomsvc

:build
if not exist build mkdir build
rc /nologo /fo build\app.res app.rc || exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Febuild\ScanReceiver.exe main.cpp ftp_server.cpp build\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib gdi32.lib || exit /b 1
echo.
echo Built: %~dp0build\ScanReceiver.exe
exit /b 0

:nomsvc
echo [ERROR] MSVC not found. Install "Visual Studio Build Tools" with the C++ workload, or use build_mingw.bat.
exit /b 1
