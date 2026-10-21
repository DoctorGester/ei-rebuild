@echo off
rem Runs a harness scenario on the standalone image with the vanilla install's data (as run_all's install
rem "standalone") and returns 0 when it passed.
rem usage: harness.cmd tests\scenarios\smoke.txt [--no-hooks] [other ei_launch arguments]
rem --no-hooks runs the same scenario on game.exe with the original code only (a baseline).
if "%~1"=="" (echo usage: harness.cmd ^<scenario^> [--no-hooks] & exit /b 2)
if not exist "%~dp0run\vanilla\game.exe" call "%~dp0tools\setup_vanilla_run.cmd" || exit /b 2
setlocal
rem The image starts from run\vanilla-clean (run\vanilla without dgVoodoo's DLLs, tools\clean_run.py); --no-hooks runs
rem run\vanilla's original game.exe with dgVoodoo.
set "EXE=%~dp0run\vanilla\game.exe"
rem The image draws with our renderer (no display mode change); the original code draws through dgVoodoo.
set MODE=--standalone
set EI_RENDER=new,present
if /i "%~2"=="--no-hooks" (
    set MODE=
    set EI_RENDER=
) else (
    set CLEAN=
    for /f "usebackq delims=" %%d in (`python "%~dp0tools\clean_run.py" vanilla`) do set "CLEAN=%%d"
    if not defined CLEAN exit /b 2
    set "EXE=%CLEAN%\game.exe"
)
rem A scenario's "# prepare:" lines write files the game reads from saves\ (tools\prepare_scenario.py).
python "%~dp0tools\prepare_scenario.py" "%~f1" "%~dp0build\verify\harness_saves" || exit /b 2
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%EXE%" --harness "%~f1" %MODE% %2 %3 %4
