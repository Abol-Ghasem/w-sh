@echo off
rem ---------------------------------------------------------------------------
rem VoiceBar - build script for Windows.
rem
rem  Option A (recommended): zig        https://ziglang.org/download/
rem      build.bat
rem
rem  Option B: Visual Studio C++ tools ("x64 Native Tools Command Prompt")
rem      build.bat msvc
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist build mkdir build

set SRC=src\core.c src\util.c src\config.c src\audio.c src\stt_server.c src\stt_cmd.c src\stt_local.c src\ui.c src\main.c
set LIBS=user32.lib gdi32.lib shell32.lib winmm.lib winhttp.lib ole32.lib oleaut32.lib advapi32.lib comctl32.lib

if /i "%~1"=="msvc" goto msvc
if /i "%~1"=="test" goto test

where zig >nul 2>nul
if errorlevel 1 goto notool

echo == release build with zig ==
zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function ^
  %SRC% -o build\VoiceBar.exe %LIBS% -Wl,--subsystem,windows -Wl,-s
if errorlevel 1 goto fail
copy /y build\VoiceBar.exe VoiceBar.exe >nul
echo built build\VoiceBar.exe  (and copied to VoiceBar.exe)
goto end

:msvc
echo == release build with MSVC ==
cl /nologo /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:build\ /Fe:build\VoiceBar.exe ^
  %SRC% /link /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 goto fail
copy /y build\VoiceBar.exe VoiceBar.exe >nul
echo built build\VoiceBar.exe
goto end

:test
echo == unit tests ==
cl /nologo /O2 /W3 /Fo:build\ /Fe:build\test_core.exe tests\test_core.c src\core.c
if errorlevel 1 goto fail
build\test_core.exe
goto end

:notool
echo zig was not found in PATH.
echo Install zig from https://ziglang.org/download/ (single zip, no installer needed)
echo or run this script from a Visual Studio developer prompt as: build.bat msvc
goto end

:fail
echo build failed.
:end
endlocal
