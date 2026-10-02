@echo off
rem Runs the verify build against the plain GOG install (no Rebirth addon or EI_Plugin), so functions
rem that Rebirth patches in memory can be hooked and verified too. The exe runs from run\vanilla
rem (GOG game.exe + dgVoodoo); game data and working directory are the GOG folder.
if not exist "%~dp0run\vanilla\game.exe" call "%~dp0tools\setup_vanilla_run.cmd" || exit /b 1
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%~dp0run\vanilla\game.exe" %*
