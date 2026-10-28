@echo off
rem Runs the recompiled game (build it first with tools\build_runtime.cmd). cdrom0: maps to game\disc\.
rem Close the game window to stop it; the console output is then complete in logs\run.log.
rem The line to look at is usually the last "[guest-branch:...]" or crash line near the end.
setlocal
set "ROOT=%~dp0.."
if not defined TEKKEN4_BUILD_DIR set "TEKKEN4_BUILD_DIR=runtime"
if defined TEKKEN4_SRC (set "TREE=%TEKKEN4_SRC%") else set "TREE=%ROOT%\..\tekken4-fork-ps2recomp"
set "EXE=%TREE%\out\%TEKKEN4_BUILD_DIR%\ps2xRuntime\ps2EntryRunner.exe"
if not exist "%EXE%" (echo ps2EntryRunner.exe not built: run tools\build_runtime.cmd & exit /b 1)
if not exist "%ROOT%\logs" mkdir "%ROOT%\logs"
rem GPU (OpenGL) GS renderer by default since 2026-10-28 (fight at 60 fps); set PS2X_GS_BACKEND=cpu for the software one.
if not defined PS2X_GS_BACKEND set "PS2X_GS_BACKEND=gl"
rem The software renderer draws on several threads; set PS2X_GS_THREADS=0 to draw on one.
if not defined PS2X_GS_THREADS set "PS2X_GS_THREADS=auto"
"%EXE%" "%ROOT%\game\disc\SCES_508.78" > "%ROOT%\logs\run.log" 2>&1
echo exit code %errorlevel%, log in logs\run.log
