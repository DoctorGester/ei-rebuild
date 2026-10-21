@echo off
rem Starts the standalone image (build\verify\ei_standalone.exe) on the plain GOG install's data in
rem F:\games\EvilIslands, with our renderer (no display mode change), to play. The image reads no game.exe
rem (S18k1); --exe only places the shared host copy (run\standalone\ei_standalone.exe, next to run\vanilla).
rem Arguments go to ei_launch (game args after --).
rem Since S21c nothing starts game.exe with our hooks (docs/notes/standalone.md section 5).
setlocal
set EI_RENDER=new,present
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" --exe "%~dp0run\vanilla\game.exe" --standalone %*
