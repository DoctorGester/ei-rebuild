@echo off
rem Runs the verify build against the plain GOG install (no Rebirth addon or EI_Plugin), so functions
rem that Rebirth patches in memory can be hooked and verified too.
"%~dp0build\verify\ei_launch.exe" --game "F:\games\EvilIslands" %*
