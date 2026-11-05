@echo off
rem Runs one scenario of tests\scenarios on the image with the vanilla install's data (run_all's install "standalone":
rem its folder is build\verify\runs\<name>-standalone) and returns 0 when it passed.
rem usage: harness.cmd tests\scenarios\smoke.txt [other run_all arguments]
rem        harness.cmd record <name> [vanilla|rebirth]
rem   record <name>   a session recorded as a scenario (docs/notes/harness.md, "Recorded play"): builds the verify build
rem                   (build\verify), runs it in a window on the vanilla data (default) or Rebirth's, your input
rem                   recorded; close the window to end it. It becomes tests\scenarios\rec_<name>.txt, or
rem                   rec_<name>_<n>.txt for a long session the game cuts into parts (tools/recorded.py).
setlocal
if /i "%~1"=="record" (
    if "%~2"=="" (
        echo harness: record needs a name ^(usage: harness.cmd record ^<name^> [vanilla^|rebirth]^)
        exit /b 2
    )
    echo harness: building the verify build ^(build\verify^)...
    call "%~dp0build.cmd" verify > "%~dp0build\record_build.log" 2>&1
    if errorlevel 1 (
        echo harness: the build failed, see build\record_build.log
        pause
        exit /b 1
    )
    python "%~dp0tools\recorded.py" play %2 %3
    exit /b
)
if "%~1"=="" (echo usage: harness.cmd ^<scenario^> ^| record ^<name^> & exit /b 2)
python "%~dp0tools\run_all.py" %~n1 --vanilla-only --retries 0 %2 %3 %4
