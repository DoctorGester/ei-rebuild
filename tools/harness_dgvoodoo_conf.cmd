@echo off
rem Harness runners only: Antialiasing and Filtering left to the game (lighter GPU load for parallel
rem runs), and CaptureMouse off (a harness game must not hold the user's cursor).
powershell -NoProfile -Command "$p='%~1'; (Get-Content $p) -replace '^(Antialiasing\s*=\s*).*$','${1}appdriven' -replace '^(Filtering\s*=\s*).*$','${1}appdriven' -replace '^(CaptureMouse\s*=\s*).*$','${1}false' | Set-Content -Encoding ascii $p" || exit /b 1
