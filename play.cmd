@echo off
rem Builds the product with every option on and starts it to play.
rem usage: play.cmd [rebirth|vanilla]
rem   rebirth (default)  the Rebirth install's data (EI_GAME_REBIRTH, default F:\games\Evil Islands (Rebirth)) with the Rebirth profile
rem                      (config\ei_rebirth_standalone.ini: widescreen, draw radii, console and script additions,
rem                      the bug fixes)
rem   vanilla            the plain GOG install's data (EI_GAME_VANILLA, default F:\games\EvilIslands) with the default flags (the bug fixes)
rem                      and widescreen
rem The UTF-8 release build (build\release-utf8, apart from the builds the suites use) is copied to run\play, so a
rem game left open never locks a build, and drawn with the smooth option and the smooth camera. The settings are
rem run\play\ei_settings.ini, next to the exe (imported once from the registry; the starter's WideScreen is the switch).
rem The performance overlay (FPS, frame and GPU times, draws; docs/notes/renderer.md section 15): add to that file
rem   [Software\Nival Interactive\EvilIslands\Standalone]
rem   "PerfOverlay"=dword:00000001
rem Vsync (docs/notes/renderer.md section 15a): on by default; to run unlocked (tearing where the system allows it) add
rem   "VSync"=dword:00000000
rem A harness game open does not end it (ST1b: harness windows have their own title).
setlocal
set MODE=%~1
if "%MODE%"=="" set MODE=rebirth
set _MSPDBSRV_ENDPOINT_=play
echo play: building the UTF-8 release (build\release-utf8)...
call "%~dp0build.cmd" release utf8 > "%~dp0build\play_build.log" 2>&1
if errorlevel 1 (
    echo play: the build failed, see build\play_build.log
    pause
    exit /b 1
)
set PLAY=%~dp0run\play
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (ei_launch.exe ei_launch.pdb ei_hook.dll ei_hook.pdb ei_standalone.exe ei_standalone.pdb) do (
    copy /y "%~dp0build\release-utf8\%%f" "%PLAY%\%%f" >nul 2>&1 || echo play: %%f in use, playing the copy already there
)
rem The Rebirth flags: ei_rebirth.ini next to ei_hook.dll (docs/notes/rebirth.md).
if not defined EI_GAME_REBIRTH set "EI_GAME_REBIRTH=F:\games\Evil Islands (Rebirth)"
if not defined EI_GAME_VANILLA set "EI_GAME_VANILLA=F:\games\EvilIslands"
set "GAME=%EI_GAME_REBIRTH%"
if /i "%MODE%"=="vanilla" (
    set "GAME=%EI_GAME_VANILLA%"
    > "%PLAY%\ei_rebirth.ini" (echo [Rebirth]& echo WideScreen=1)
) else (
    copy /y "%~dp0config\ei_rebirth_standalone.ini" "%PLAY%\ei_rebirth.ini" >nul
)
set EI_RENDER=new,present,smooth,smoothcamera
cd /d "%PLAY%"
rem No --exe: ei_launch starts the ei_standalone.exe next to it, so the settings file is run\play's too.
"%PLAY%\ei_launch.exe" --game "%GAME%" --standalone
