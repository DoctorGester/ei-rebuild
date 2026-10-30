@echo off
rem Builds the standalone image (ei-rebuild.exe, with ei_harness.dll in the verify build), ei_launch.exe and the tools (32-bit) with
rem the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug|play|asan|x64]
rem   release  the product: the standalone image
rem   verify   the harness build: the same image plus the harness (tests/verify) and the vector recorders
rem            (tests/vectors), for run_all / work.py test (the image's runs, and the original game.exe's
rem            with --no-hooks for goldens and EI_VECTORS recording)
rem   debug    the release build unoptimised
rem   play     the release build into build\play, apart from the builds the suites use (play.cmd)
rem   asan     the verify build with AddressSanitizer, into build\asan (runs: set EI_BUILD_DIR to it;
rem            docs/notes/harness.md, "AddressSanitizer")
rem   x64      the release build for 64-bit Windows, into build\x64: the image and the offline checks (the harness and
rem            the function vectors are 32-bit only; docs/notes/standalone.md, "The x64 build")
rem Neither the build nor its checks read game.exe (C25c, C25d).
setlocal
rem EI_VSROOT: the Build Tools' folder when it is not the default below.
if defined EI_VSROOT (set "VSROOT=%EI_VSROOT%") else (set "VSROOT=E:\bin\vs\buildtools")
set CFG=%~1
if "%CFG%"=="" set CFG=release

set VCVARS=vcvars32
if /i "%CFG%"=="x64" set VCVARS=vcvars64
call "%VSROOT%\VC\Auxiliary\Build\%VCVARS%.bat" >nul || exit /b 1
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
rem The checks (tools/build_checks.py, side by side): the cleanup and comment counts, the data seam, the harness key,
rem the generated and listed files, the function vectors (ei_vectors), the interface canvases (ei_ui_check) and the
rem performance overlay (ei_perf_check).
set CHECKS=
if /i "%CFG%"=="x64" set CHECKS=--x64
python "%~dp0tools\build_checks.py" "%OUT%" %CHECKS% || exit /b 1
echo Built: %OUT%
