@echo off
rem Builds run\vanilla: a copy of the plain GOG game.exe for the tools that read it (tools/exe_path.py: the
rem generators and find_*). Until H40 it also held what the original's harness runs needed (Miles, Bink, dgVoodoo's
rem DLLs and conf); nothing runs game.exe now. No game folder is modified; run\ is not committed.
setlocal
set VANILLA=F:\games\EvilIslands
set OUT=%~dp0..\run\vanilla
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%VANILLA%\game.exe" "%OUT%\" >nul || exit /b 1
echo Prepared %OUT%
