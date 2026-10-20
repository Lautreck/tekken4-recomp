@echo off
rem Runs the recompiled game (build it first with tools\build_runtime.cmd). cdrom0: maps to game\disc\.
rem Close the game window to stop it; the console output is then complete in logs\run.log.
rem The line to look at is usually the last "[guest-branch:...]" or crash line near the end.
setlocal
set "ROOT=%~dp0.."
set "EXE=%ROOT%\external\PS2Recomp\out\runtime\ps2xRuntime\ps2EntryRunner.exe"
if not exist "%EXE%" (echo ps2EntryRunner.exe not built: run tools\build_runtime.cmd & exit /b 1)
if not exist "%ROOT%\logs" mkdir "%ROOT%\logs"
"%EXE%" "%ROOT%\game\disc\SCES_508.78" > "%ROOT%\logs\run.log" 2>&1
echo exit code %errorlevel%, log in logs\run.log
