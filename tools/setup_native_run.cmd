@echo off
rem Builds run\native: run\vanilla without dgVoodoo (no DDraw.dll, D3DImm.dll, D3D8.dll, D3D9.dll or
rem dgVoodoo.conf next to game.exe). Only for the switch-over without DirectDraw (EI_RENDER
rem "new,present", ticket R9a, docs/notes/renderer.md section 9): the new renderer presents through
rem its own swap chain, so the game makes no DirectDraw or Direct3D 7 device. tools\run_all.py
rem --native runs on it. Makes run\vanilla first when it is missing (tools\setup_vanilla_run.cmd).
rem No game folder is modified; run\ is not committed.
setlocal
set VANILLA_RUN=%~dp0..\run\vanilla
set OUT=%~dp0..\run\native
if not exist "%VANILLA_RUN%\game.exe" call "%~dp0setup_vanilla_run.cmd" || exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
for %%f in (DDraw.dll D3DImm.dll D3D8.dll D3D9.dll dgVoodoo.conf dgVoodoo.conf.orig) do if exist "%OUT%\%%f" del "%OUT%\%%f"
for %%f in (game.exe binkw32.dll mss32.dll mp3dec.asi 2dintmmx.dll 3dfpfpu.dll) do copy /y "%VANILLA_RUN%\%%f" "%OUT%\" >nul || exit /b 1
echo Prepared %OUT%
