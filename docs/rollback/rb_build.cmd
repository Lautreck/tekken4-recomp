@echo off
rem Stages generated code + the COMMITTED shared overrides (HEAD of D:\tekken4-recomp, so other agents' uncommitted
rem edits stay out) + the rollback override, then builds out\rb.
set TEKKEN4_SRC=D:\tekken4-fork-rb
set TEKKEN4_BUILD_DIR=rb
call D:\tekken4-recomp\tools\stage_generated.cmd
if errorlevel 1 exit /b 1
set "RUN=D:\tekken4-fork-rb\ps2xRuntime\src\runner"
set "STG=D:\tekken4-fork-rb\out\rbstage"
for %%F in (tekken4_overrides.cpp tekken4_frameshot_overrides.cpp tekken4_profiler_overrides.cpp tekken4_testseed_overrides.cpp) do (
  git -C D:\tekken4-recomp show HEAD:runtime/%%F > "%STG%\%%F"
  fc /b "%STG%\%%F" "%RUN%\%%F" >nul || copy /y "%STG%\%%F" "%RUN%\%%F" >nul
)
rem The rollback driver sits in runtime\rollback\ (not staged into other trees until patch 33 is in patches\).
copy /y D:\tekken4-recomp\runtime\rollback\tekken4_rollback_overrides.cpp "%RUN%\" >nul
call D:\tekken4-recomp\tools\build_runtime.cmd
