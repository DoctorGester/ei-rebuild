@echo off
rem Builds the standalone image (ei_standalone.exe, ei_hook.dll), ei_launch.exe and the tools (32-bit) with
rem the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug|play|asan]
rem   release  the product: the standalone image
rem   verify   the harness build: the same image plus the harness (tests/verify) and the vector recorders
rem            (tests/vectors), for run_all / work.py test (the image's runs, and the original game.exe's
rem            with --no-hooks for goldens and EI_VECTORS recording)
rem   debug    the release build unoptimised
rem   play     the release build into build\play, apart from the builds the suites use (play.cmd)
rem   asan     the verify build with AddressSanitizer, into build\asan (runs: set EI_BUILD_DIR to it;
rem            docs/notes/harness.md, "AddressSanitizer")
rem Neither the build nor its checks read game.exe (C25c, C25d).
setlocal
rem EI_VSROOT: the Build Tools' folder when it is not the default below.
if defined EI_VSROOT (set "VSROOT=%EI_VSROOT%") else (set "VSROOT=E:\bin\vs\buildtools")
set CFG=%~1
if "%CFG%"=="" set CFG=release

call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
rem One PDB server per checkout and configuration: the 32-bit mspdbsrv runs out of its 4 GB when every
rem worktree's build shares the default one (C2471 / C1060 / C1090 / LNK1318).
for %%I in ("%~dp0.") do set "_MSPDBSRV_ENDPOINT_=ei_%%~nxI_%~1"

set HARNESS=OFF
set ASAN=OFF
set BUILDTYPE=RelWithDebInfo
if /i "%CFG%"=="verify" set HARNESS=ON
if /i "%CFG%"=="asan" set HARNESS=ON
if /i "%CFG%"=="asan" set ASAN=ON
if /i "%CFG%"=="debug" set BUILDTYPE=Debug
set OUT=%~dp0build\%CFG%

cmake -S "%~dp0." -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DEI_HARNESS=%HARNESS% -DEI_ASAN=%ASAN% || exit /b 1
cmake --build "%OUT%" || exit /b 1
rem Cleanup-plan patterns (offsets, VCall, __asm, raw addresses) may only go down per file.
python "%~dp0tools\check_cleanup.py" || exit /b 1
rem Addresses and ticket ids in comments may only go down per file (docs/comment-guide.md, C22).
python "%~dp0tools\comments.py" check || exit /b 1
rem Game data by address only through the data seam (src/game/game_data.h, S18): sealed sections none raw,
rem the rest per file no more than config/data_seam.csv allows.
python "%~dp0tools\data_seam.py" --check || exit /b 1
rem game.exe's side of the harness reads game.exe's objects through its own views, ours only through
rem tests/verify/image: no class of ours in the reference cache's key (tools/run_all.py, harness_files; H30b).
python "%~dp0tools\check_harness_key.py" || exit /b 1
rem Generated and listed files against what they come from, without game.exe (C25d): the import files (gameapi.h by
rem hand, a harness route on a slot), the named ranges' csv and their rules, the retired vtables and committed cells.
python "%~dp0tools\gen_iat.py" --check || exit /b 1
python "%~dp0tools\named_data.py" --check || exit /b 1
python "%~dp0tools\gen_image_cells.py" --check || exit /b 1
rem Every row of config/static_init_ports.csv names a definition in its file (tools/gen_static_init.py, C56).
python "%~dp0tools\gen_static_init.py" --check-ports || exit /b 1
rem Function vectors recorded from the original, replayed against our code without the game (tests/vectors).
"%OUT%\ei_vectors.exe" || exit /b 1
rem The interface canvases (src/render/render_ui) on a WARP device against CPU rules and GDI (tools/ui_check).
"%OUT%\ei_ui_check.exe" || exit /b 1
rem The performance overlay (src/render/render_perf) on a WARP device: text, pixels, GPU timestamps (tools/perf_check).
"%OUT%\ei_perf_check.exe" || exit /b 1
echo Built: %OUT%
