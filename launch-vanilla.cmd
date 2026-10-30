@echo off
rem Starts the standalone image on the plain GOG install's data (EI_GAME_VANILLA, default F:\games\EvilIslands), with our renderer (no
rem display mode change) and the plain install's Rebirth profile (the bug fixes and widescreen; ei_settings.ini's
rem [patches] overrides it), to play. It plays from a copy of the release
rem build in run\play, so a game left open never locks build\ (main's builds and suites keep working); the game's
rem settings are run\play\ei_settings.ini, next to the exe. The copy is refreshed at each start unless a game still
rem runs from it. play.cmd builds and plays the UTF-8 build with every option on. Arguments go to ei_launch (game
rem args after --).
setlocal
if not defined EI_GAME_VANILLA set "EI_GAME_VANILLA=F:\games\EvilIslands"
set PLAY=%~dp0run\play
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (ei_launch.exe ei_launch.pdb ei-rebuild.exe ei-rebuild.pdb) do (
    copy /y "%~dp0build\release\%%f" "%PLAY%\%%f" >nul 2>&1 || echo launch-vanilla: %%f in use, playing the copy already there
)
rem The renderer's default options (play.cmd turns on the smooth option and the smooth camera).
set EI_RENDER=
cd /d "%PLAY%"
rem No --exe: ei_launch starts the ei-rebuild.exe next to it.
"%PLAY%\ei_launch.exe" --game "%EI_GAME_VANILLA%" --standalone %*
