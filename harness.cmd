@echo off
rem Runs one scenario of tests\scenarios on the image with the vanilla install's data (run_all's install "standalone":
rem its folder is build\verify\runs\<name>-standalone) and returns 0 when it passed.
rem usage: harness.cmd tests\scenarios\smoke.txt [other run_all arguments]
if "%~1"=="" (echo usage: harness.cmd ^<scenario^> & exit /b 2)
python "%~dp0tools\run_all.py" %~n1 --vanilla-only --retries 0 %2 %3 %4
