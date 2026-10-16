@echo off
rem Starts the standalone image (build\verify\ei_standalone.exe) on the plain GOG install's data in
rem F:\games\EvilIslands, with our renderer (no display mode change), to play: run\vanilla's game.exe
rem (GOG game.exe + dgVoodoo) laid out without its code. Arguments go to ei_launch (game args after --).
rem Since S21c nothing starts game.exe with our hooks (docs/notes/standalone.md section 5).
if not exist "%~dp0run\vanilla\game.exe" call "%~dp0tools\setup_vanilla_run.cmd" || exit /b 1
setlocal
set EI_RENDER=new,present
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%~dp0run\vanilla\game.exe" --standalone %*
