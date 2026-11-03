@echo off
rem Starts the standalone image on the plain GOG install's data (EI_GAME_VANILLA, default F:\games\EvilIslands), with our renderer (no
rem display mode change) and the plain install's Rebirth profile (the bug fixes and widescreen; ei_settings.ini's
rem [patches] overrides it), to play. It plays from a copy of the release build in build\play_game, so a game left open
rem never locks build\ (main's builds and suites keep working); the game's settings are build\play_game\ei_settings.ini,
rem next to the exe. The copy is refreshed at each start unless a game still runs from it. play.cmd builds and plays
rem the build with every option on. Arguments are the game's (after "--").
setlocal
if not defined EI_GAME_VANILLA set "EI_GAME_VANILLA=F:\games\EvilIslands"
set PLAY=%~dp0build\play_game
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (ei-rebuild.exe ei-rebuild.pdb third_party_notices.txt) do (
    copy /y "%~dp0build\release\%%f" "%PLAY%\%%f" >nul 2>&1 || echo launch-vanilla: %%f in use, playing the copy already there
)
rem The renderer's default options (play.cmd turns on the smooth option and the smooth camera).
set EI_RENDER=
set "EI_GAME_ROOT=%EI_GAME_VANILLA%"
start "" /D "%EI_GAME_ROOT%" "%PLAY%\ei-rebuild.exe" -- %*
