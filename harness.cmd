@echo off
rem Runs a harness scenario on the standalone image with the vanilla install's data (as run_all's install
rem "standalone") and returns 0 when it passed.
rem usage: harness.cmd tests\scenarios\smoke.txt [other ei_launch arguments]
if "%~1"=="" (echo usage: harness.cmd ^<scenario^> & exit /b 2)
setlocal
if not defined EI_GAME_VANILLA set "EI_GAME_VANILLA=F:\games\EvilIslands"
rem The image draws with our renderer (no display mode change); EI_RENDER does not reach it: a scenario's "render"
rem step sets its renderer options.
set EI_RENDER=
rem A scenario's "# prepare:" lines write files the game reads from saves\ (tools\prepare_scenario.py).
python "%~dp0tools\prepare_scenario.py" "%~f1" "%~dp0build\verify\harness_saves" || exit /b 2
"%~dp0build\verify\ei_launch.exe" --game "%EI_GAME_VANILLA%" --harness "%~f1" --standalone %2 %3 %4
