@echo off
rem Starts the standalone image (build\verify\ei_standalone.exe) on the plain GOG install's data in
rem F:\games\EvilIslands, with our renderer (no display mode change), to play: run\vanilla-clean's game.exe
rem (the GOG game.exe) laid out without its code. run\vanilla-clean is run\vanilla's files without dgVoodoo's DLLs
rem (tools\clean_run.py makes or updates it), so the DirectX check takes the native path.
rem Arguments go to ei_launch (game args after --).
rem Since S21c nothing starts game.exe with our hooks (docs/notes/standalone.md section 5).
if not exist "%~dp0run\vanilla\game.exe" call "%~dp0tools\setup_vanilla_run.cmd" || exit /b 1
setlocal
set CLEAN=
for /f "usebackq delims=" %%d in (`python "%~dp0tools\clean_run.py" vanilla`) do set "CLEAN=%%d"
if not defined CLEAN exit /b 1
set EI_RENDER=new,present
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%CLEAN%\game.exe" --standalone %*
