@echo off
rem Builds run\vanilla: the plain GOG game.exe with Rebirth's dgVoodoo DLLs next to it (vanilla
rem DirectDraw does not work on modern Windows). No game folder is modified; run\ is not committed.
setlocal
set VANILLA=F:\games\EvilIslands
set REBIRTH_ENGINE=F:\games\Evil Islands (Rebirth)\starter\Engine
set OUT=%~dp0..\run\vanilla
if not exist "%OUT%" mkdir "%OUT%"
for %%f in (game.exe binkw32.dll mss32.dll mp3dec.asi 2dintmmx.dll 3dfpfpu.dll) do copy /y "%VANILLA%\%%f" "%OUT%\" >nul || exit /b 1
for %%f in (DDraw.dll D3DImm.dll D3D8.dll D3D9.dll dgVoodoo.conf) do copy /y "%REBIRTH_ENGINE%\%%f" "%OUT%\" >nul || exit /b 1
echo Prepared %OUT%
