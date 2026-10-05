@echo off
rem Runs tools\check_returns.py with the VS tools (undname) on PATH.
call "E:\bin\vs\buildtools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
python "%~dp0check_returns.py" %*
