@echo off
rem Builds the standalone image (ei_standalone.exe, ei_hook.dll), ei_launch.exe and the tools (32-bit) with
rem the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug]
rem   release  the product: the standalone image
rem   verify   the harness build: the same image plus the harness (tests/verify) and the vector recorders
rem            (tests/vectors), for run_all / work.py test (the image's runs, and the original game.exe's
rem            with --no-hooks for goldens and EI_VECTORS recording)
rem   debug    the release build unoptimised
setlocal
set VSROOT=E:\bin\vs\buildtools
set CFG=%~1
if "%CFG%"=="" set CFG=release

call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

set HARNESS=OFF
set BUILDTYPE=RelWithDebInfo
if /i "%CFG%"=="verify" set HARNESS=ON
if /i "%CFG%"=="debug" set BUILDTYPE=Debug

cmake -S "%~dp0." -B "%~dp0build\%CFG%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DEI_HARNESS=%HARNESS% || exit /b 1
cmake --build "%~dp0build\%CFG%" || exit /b 1
rem Hooks registered through a virtual-call thunk recurse forever (rewrite-guide rule 4).
python "%~dp0tools\check_thunks.py" || exit /b 1
rem One address, one registration per kind (replacement, recorder), read from the built DLL (SV6d).
python "%~dp0tools\check_registrations.py" --dll "%~dp0build\%CFG%\ei_hook.dll" || exit /b 1
rem Double constants folded from a product the original computes at 24 bits (SV7d1a); not in the debug build.
if /i not "%CFG%"=="debug" python "%~dp0tools\check_fp_fold.py" --dll "%~dp0build\%CFG%\ei_hook.dll" || exit /b 1
rem New duplicate EI_ORIGINAL forwarders: use the shared src/game/originals_<area>.h (ticket C3).
python "%~dp0tools\check_forwarders.py" || exit /b 1
rem Cleanup-plan patterns (offsets, VCall, __asm, raw addresses) may only go down per file.
python "%~dp0tools\check_cleanup.py" || exit /b 1
rem Functions under five bytes (EI_REPLACE_SHORT): each port's kind against game.exe's bytes and our
rem compiled code (not in the debug build, whose code is unoptimised).
set SHORTMAP=--map "%~dp0build\%CFG%\ei_hook.map"
if /i "%CFG%"=="debug" set SHORTMAP=
python "%~dp0tools\check_short_functions.py" %SHORTMAP% || exit /b 1
rem The standalone image's static initialisers (src/standalone/static_init.cpp) against game.exe's.
python "%~dp0tools\check_static_init.py" || exit /b 1
rem Function vectors recorded from the original, replayed against our code without the game (tests/vectors).
"%~dp0build\%CFG%\ei_vectors.exe" || exit /b 1
echo Built: %~dp0build\%CFG%
