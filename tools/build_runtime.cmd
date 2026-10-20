@echo off
rem Builds PS2Recomp's ps2EntryRunner (RelWithDebInfo) with MSVC + Ninja. Run tools\stage_generated.cmd first.
rem Output: external\PS2Recomp\out\runtime\ps2xRuntime\ps2EntryRunner.exe
rem Logs: per-function trace (AGRESSIVE_LOGS) off, it is what slowed the GoW port down; console runtime
rem logs and the IOP RPC trace stay on while we chase boot problems. Changing any of these rebuilds everything.
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo MSVC not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
set "SRC=%~dp0..\external\PS2Recomp"
rem Our runtime patches (patches\*.patch, against the pinned PS2Recomp commit); already-applied ones are skipped.
set "PATCHFAIL="
for %%P in ("%~dp0..\patches\*.patch") do call :apply "%%~fP"
if defined PATCHFAIL exit /b 1
cmake -S "%SRC%" -B "%SRC%\out\runtime" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF ^
  -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_RUNTIME_LOGS=ON -DPS2X_ENABLE_IOP_RPC_TRACE=ON
if errorlevel 1 exit /b 1
cmake --build "%SRC%\out\runtime" --target ps2EntryRunner
if errorlevel 1 exit /b 1
exit /b 0

rem Applies one patch unless it is already in the tree. If it neither reverses nor applies cleanly, the tree has
rem local edits on top of it (debugging): build what is there, but say so.
:apply
git -C "%SRC%" apply --reverse --check "%~1" >nul 2>&1
if not errorlevel 1 exit /b 0
git -C "%SRC%" apply --check "%~1" >nul 2>&1
if errorlevel 1 (
  echo WARNING: %~nx1 is neither applied nor applicable: building the PS2Recomp tree as it is ^(local edits?^)
  exit /b 0
)
git -C "%SRC%" apply "%~1"
if errorlevel 1 (
  echo patch %~nx1 failed to apply
  set "PATCHFAIL=1"
  exit /b 1
)
echo applied %~nx1
exit /b 0
