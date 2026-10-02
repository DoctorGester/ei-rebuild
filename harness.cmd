@echo off
rem Runs a harness scenario on the vanilla install and returns 0 when no verify check failed.
rem usage: harness.cmd tests\scenarios\smoke.txt [--no-hooks]
rem --no-hooks runs the same scenario on the original code only (a baseline).
if "%~1"=="" (echo usage: harness.cmd ^<scenario^> & exit /b 2)
if not exist "%~dp0run\vanilla\game.exe" call "%~dp0tools\setup_vanilla_run.cmd" || exit /b 2
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%~dp0run\vanilla\game.exe" --harness "%~f1" %2 %3
