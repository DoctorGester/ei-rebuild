@echo off
rem Starts the standalone image on the plain GOG install's data in F:\games\EvilIslands, with our renderer (no
rem display mode change) and the default Rebirth flags (the bug fixes), to play. It plays from a copy of the release
rem build in run\play, so a game left open never locks build\ (main's builds and suites keep working); the game's
rem settings are run\play\ei_settings.ini, next to the exe. The copy is refreshed at each start unless a game still
rem runs from it. play.cmd builds and plays the UTF-8 build with every option on. Arguments go to ei_launch (game
rem args after --).
setlocal
set PLAY=%~dp0run\play
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (ei_launch.exe ei_launch.pdb ei_hook.dll ei_hook.pdb ei_standalone.exe ei_standalone.pdb) do (
    copy /y "%~dp0build\release\%%f" "%PLAY%\%%f" >nul 2>&1 || echo launch-vanilla: %%f in use, playing the copy already there
)
rem The default Rebirth flags (play.cmd may have left an ei_rebirth.ini there).
del "%PLAY%\ei_rebirth.ini" 2>nul
set EI_RENDER=new,present
cd /d "%PLAY%"
rem No --exe: ei_launch starts the ei_standalone.exe next to it.
"%PLAY%\ei_launch.exe" --game "F:\games\EvilIslands" --standalone %*
