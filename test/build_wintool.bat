@echo off
rem Builds test\wintool.cpp into test\out\wintool.exe (MSVC). Keeps the app's build\ clean.
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
if not exist out mkdir out
cl /nologo /utf-8 /EHsc /O2 /D_CRT_SECURE_NO_WARNINGS /Foout\ /Feout\wintool.exe wintool.cpp user32.lib gdi32.lib winmm.lib || exit /b 1
del out\wintool.obj
echo Built: %CD%\out\wintool.exe
exit /b 0

:nomsvc
echo [ERROR] MSVC not found.
exit /b 1
