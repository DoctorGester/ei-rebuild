@echo off
rem Builds run\rebirth: a copy of Rebirth's game.exe (one code byte differs from the plain one) for the tools that
rem read it (tools/exe_path.py --all). Until H40 it also held what the original's harness runs needed (the addon
rem loader, dgVoodoo's DLLs and conf); nothing runs game.exe now. No game folder is modified; run\ is not committed.
setlocal
set REBIRTH_ENGINE=F:\games\Evil Islands (Rebirth)\starter\Engine
set OUT=%~dp0..\run\rebirth
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%REBIRTH_ENGINE%\game.exe" "%OUT%\" >nul || exit /b 1
echo Prepared %OUT%
