@echo off
rem Builds run\rebirth: Rebirth's engine folder (game.exe, its addon loader dinput.dll and the dgVoodoo
rem DLLs) with a dgVoodoo.conf of our own, for harness runs. The install's conf forces 8x antialiasing
rem and 16x texture filtering, which limits how many games run at once; ours leaves both to the game.
rem No game folder is modified; run\ is not committed.
setlocal
set REBIRTH_ENGINE=F:\games\Evil Islands (Rebirth)\starter\Engine
set OUT=%~dp0..\run\rebirth
if not exist "%OUT%" mkdir "%OUT%"
for %%f in (game.exe dinput.dll DDraw.dll D3DImm.dll D3D8.dll D3D9.dll dgVoodoo.conf) do copy /y "%REBIRTH_ENGINE%\%%f" "%OUT%\" >nul || exit /b 1
call "%~dp0harness_dgvoodoo_conf.cmd" "%OUT%\dgVoodoo.conf" || exit /b 1
echo Prepared %OUT%
