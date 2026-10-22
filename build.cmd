@echo off
rem Builds the standalone image (ei_standalone.exe, ei_hook.dll), ei_launch.exe and the tools (32-bit) with
rem the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug] [utf8|trial]
rem   release  the product: the standalone image
rem   verify   the harness build: the same image plus the harness (tests/verify) and the vector recorders
rem            (tests/vectors), for run_all / work.py test (the image's runs, and the original game.exe's
rem            with --no-hooks for goldens and EI_VECTORS recording)
rem   debug    the release build unoptimised
rem   utf8     with EI_UTF8 (every string inside the game in UTF-8, src/game/asset_text.h), into
rem            build\<cfg>-utf8 (run_all / work.py test with EI_BUILD_DIR=build\verify-utf8)
rem   trial    C25's zero-filled trial in the same folder: the image's data bytes zero but the cells and
rem            config/image_kept.csv (tools/gen_image_cells.py --zero); every other build turns it off again
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
set UTF8=OFF
set TRIAL=OFF
set OUT=%~dp0build\%CFG%
if /i "%~2"=="utf8" set UTF8=ON
if /i "%~2"=="utf8" set OUT=%~dp0build\%CFG%-utf8
if /i "%~2"=="trial" set TRIAL=ON

cmake -S "%~dp0." -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DEI_HARNESS=%HARNESS% -DEI_UTF8=%UTF8% -DEI_IMAGE_ZERO_TRIAL=%TRIAL% || exit /b 1
cmake --build "%OUT%" || exit /b 1
rem The checks that compare with game.exe run when tools/exe_path.py finds one (run\vanilla or run\rebirth,
rem EI_EXE); without one (or with EI_NO_GAME_EXE=1, C25) they are skipped with one line.
set GAMEEXE=1
python "%~dp0tools\exe_path.py" >nul 2>nul || set GAMEEXE=0
if "%GAMEEXE%"=="0" echo build.cmd: no game.exe ^(tools/exe_path.py^): skipped the checks against it: check_fp_fold, rdata_literals, named_data, check_short_functions, check_static_init, gen_image_cells --check, gen_iat --check
rem Hooks registered through a virtual-call thunk recurse forever (rewrite-guide rule 4).
python "%~dp0tools\check_thunks.py" || exit /b 1
rem One address, one registration per kind (replacement, recorder), read from the built DLL (SV6d).
python "%~dp0tools\check_registrations.py" --dll "%OUT%\ei_hook.dll" || exit /b 1
rem Double constants folded from a product the original computes at 24 bits (SV7d1a); not in the debug build.
if "%GAMEEXE%"=="1" if /i not "%CFG%"=="debug" python "%~dp0tools\check_fp_fold.py" --dll "%OUT%\ei_hook.dll" || exit /b 1
rem New duplicate EI_ORIGINAL forwarders: use the shared src/game/originals_<area>.h (ticket C3).
python "%~dp0tools\check_forwarders.py" || exit /b 1
rem Cleanup-plan patterns (offsets, VCall, __asm, raw addresses) may only go down per file.
python "%~dp0tools\check_cleanup.py" || exit /b 1
rem Game data by address only through the data seam (src/game/game_data.h, S18): sealed sections none raw,
rem the rest per file no more than config/data_seam.csv allows.
python "%~dp0tools\data_seam.py" --check || exit /b 1
rem Literals standing for game.exe's .rdata constants (/*0073B86C*/ tags) against its bytes (S18e).
if "%GAMEEXE%"=="1" python "%~dp0tools\rdata_literals.py" --check || exit /b 1
rem Named globals in place of game.exe's data (/*0074111C*/ {...} initialisers) against its bytes, and nothing
rem else reaching their ranges, whose bytes the image's copy leaves zero (config/named_data.csv, C18).
if "%GAMEEXE%"=="1" python "%~dp0tools\named_data.py" --check || exit /b 1
rem Functions under five bytes (EI_REPLACE_SHORT): each port's kind against game.exe's bytes and our
rem compiled code (not in the debug build, whose code is unoptimised).
set SHORTMAP=--map "%OUT%\ei_hook.map"
if /i "%CFG%"=="debug" set SHORTMAP=
if "%GAMEEXE%"=="1" python "%~dp0tools\check_short_functions.py" %SHORTMAP% || exit /b 1
rem The standalone image's static initialisers (src/standalone/static_init.cpp) against game.exe's.
if "%GAMEEXE%"=="1" python "%~dp0tools\check_static_init.py" || exit /b 1
rem The image's pointer cells into .rdata and .data (src/standalone/image_cells.inc, S18b, S18d, S18h) against game.exe's data.
if "%GAMEEXE%"=="1" python "%~dp0tools\gen_image_cells.py" --check || exit /b 1
rem The image's import table (src/standalone/image_imports.inc, S18f) and gameapi.h against game.exe's imports.
if "%GAMEEXE%"=="1" python "%~dp0tools\gen_iat.py" --check || exit /b 1
rem Function vectors recorded from the original, replayed against our code without the game (tests/vectors).
"%OUT%\ei_vectors.exe" || exit /b 1
rem The interface canvases (src/render/render_ui) on a WARP device against CPU rules and GDI (tools/ui_check).
"%OUT%\ei_ui_check.exe" || exit /b 1
rem The performance overlay (src/render/render_perf) on a WARP device: text, pixels, GPU timestamps (tools/perf_check).
"%OUT%\ei_perf_check.exe" || exit /b 1
echo Built: %OUT%
