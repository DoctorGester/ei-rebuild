@echo off
rem Sets Antialiasing and Filtering to appdriven in a dgVoodoo.conf (harness runners only).
powershell -NoProfile -Command "$p='%~1'; (Get-Content $p) -replace '^(Antialiasing\s*=\s*).*$','${1}appdriven' -replace '^(Filtering\s*=\s*).*$','${1}appdriven' | Set-Content -Encoding ascii $p" || exit /b 1
