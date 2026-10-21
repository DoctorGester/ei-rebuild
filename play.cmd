@echo off
rem Builds the product with every option on and starts it to play.
rem usage: play.cmd [rebirth|vanilla]
rem   rebirth (default)  the Rebirth install's data (F:\games\Evil Islands (Rebirth)) with the Rebirth profile
rem                      (config\ei_rebirth_standalone.ini: widescreen, draw radii, console and script additions,
rem                      the bug fixes)
rem   vanilla            the plain GOG install's data (F:\games\EvilIslands) with the default flags (the bug fixes)
rem                      and widescreen
rem The UTF-8 release build (build\release-utf8, apart from the builds the suites use) is copied to run\play, so a
rem game left open never locks a build, and drawn with the smooth option and the smooth camera. The settings are
rem run\standalone\ei_settings.ini (imported once from the registry; the starter's WideScreen is the switch).
rem Until ST1b, the game ends at once when a harness game is open (the original's one-instance check).
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
set GAME=F:\games\Evil Islands (Rebirth)
if /i "%MODE%"=="vanilla" (
    set GAME=F:\games\EvilIslands
    > "%PLAY%\ei_rebirth.ini" (echo [Rebirth]& echo WideScreen=1)
) else (
    copy /y "%~dp0config\ei_rebirth_standalone.ini" "%PLAY%\ei_rebirth.ini" >nul
)
set EI_RENDER=new,present,smooth,smoothcamera
cd /d "%PLAY%"
"%PLAY%\ei_launch.exe" --game "%GAME%" --exe "%~dp0run\vanilla\game.exe" --standalone
