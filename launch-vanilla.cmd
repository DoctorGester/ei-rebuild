@echo off
rem Starts the standalone image on the plain GOG install's data in F:\games\EvilIslands, with our renderer (no
rem display mode change), to play. The image reads no game.exe (S18k1); --exe only places the shared host copy
rem (run\standalone\ei_standalone.exe, next to run\vanilla), whose folder also holds the game's ei_settings.ini.
rem It plays from a copy of the release build in run\play, so a game left open never locks build\ (main's builds
rem and suites keep working) and its log stays out of the build's. The copy is refreshed at each start unless a
rem game still runs from it. Arguments go to ei_launch (game args after --).
setlocal
set PLAY=%~dp0run\play
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (ei_launch.exe ei_launch.pdb ei_hook.dll ei_hook.pdb ei_standalone.exe ei_standalone.pdb) do (
    copy /y "%~dp0build\release\%%f" "%PLAY%\%%f" >nul 2>&1 || echo launch-vanilla: %%f in use, playing the copy already there
)
set EI_RENDER=new,present
cd /d "%PLAY%"
"%PLAY%\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%~dp0run\vanilla\game.exe" --standalone %*
