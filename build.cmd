@echo off
rem Builds the standalone image (ei-rebuild.exe) and the tools for 64-bit Windows (x64) with the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug|asan|coverage]
rem   release  the product: the standalone image
rem   verify   the harness build: the same image plus the harness (tests/verify), for run_all / work.py test
rem   debug    the release build unoptimised
rem   asan     the verify build with AddressSanitizer, into build\asan (runs: set EI_BUILD_DIR to it;
rem            docs/notes/harness.md, "AddressSanitizer")
rem   coverage the verify build's ei-rebuild.exe with source coverage, into build\coverage, without the build checks
rem            (tools/coverage.py runs the suites on it; docs/notes/harness.md, "Source coverage")
rem Neither the build nor its checks read game.exe.
setlocal
rem EI_VSROOT: the Build Tools' folder when it is not the default below.
if defined EI_VSROOT (set "VSROOT=%EI_VSROOT%") else (set "VSROOT=E:\bin\vs\buildtools")
set CFG=%~1
if "%CFG%"=="" set CFG=release
rem The build gate (tools/build_gate.py; docs/notes/work-board.md, "Builds"): this file runs again under it, which
rem waits while a quiet test run holds the machine, holds the build's ticket in build/live/builds and gives the inner
rem run EI_BUILD_STATUS, where its stage and ninja's progress go for the work board.
if not defined EI_BUILD_STATUS python "%~dp0tools\build_gate.py" "%~f0" %* && exit /b 0 || exit /b 1

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
rem One PDB server per checkout and configuration: the builds of other worktrees running at the same time share none.
for %%I in ("%~dp0.") do set "_MSPDBSRV_ENDPOINT_=ei_%%~nxI_%~1"

set HARNESS=OFF
set ASAN=OFF
set COVERAGE=OFF
set TARGET=
set BUILDTYPE=RelWithDebInfo
if /i "%CFG%"=="verify" set HARNESS=ON
if /i "%CFG%"=="asan" set HARNESS=ON
if /i "%CFG%"=="asan" set ASAN=ON
if /i "%CFG%"=="coverage" set HARNESS=ON
if /i "%CFG%"=="coverage" set COVERAGE=ON
if /i "%CFG%"=="coverage" set TARGET=--target ei-rebuild
if /i "%CFG%"=="debug" set BUILDTYPE=Debug
set OUT=%~dp0build\%CFG%
rem The compiler cache (tools/compiler_cache.py: sccache; docs/notes/standalone.md, "The compiler cache"): its server
rem started and EI_COMPILER_LAUNCHER, empty without it. A build folder's first build goes through it; later builds
rem compile directly (EI_COMPILER_CACHE_DIRECT).
set EI_COMPILER_CACHE_DIRECT=
if exist "%OUT%\.ninja_log" set EI_COMPILER_CACHE_DIRECT=1
set EI_COMPILER_LAUNCHER=
set EI_STEP_WRAPPER=
for /f "delims=" %%L in ('python "%~dp0tools\compiler_cache.py" %CFG%') do set "%%L"

rem A build folder configured with another compiler (a 32-bit one, before the x64-only build) is configured afresh.
set FRESH=
if exist "%OUT%\CMakeCache.txt" findstr /c:"Hostx64/x64/cl.exe" "%OUT%\CMakeCache.txt" >nul || set FRESH=--fresh
cmake %FRESH% -S "%~dp0." -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DEI_HARNESS=%HARNESS% -DEI_ASAN=%ASAN% -DEI_COVERAGE=%COVERAGE% ^
    "-DCMAKE_C_COMPILER_LAUNCHER=%EI_COMPILER_LAUNCHER%" "-DCMAKE_CXX_COMPILER_LAUNCHER=%EI_COMPILER_LAUNCHER%" ^
    "-DCMAKE_CXX_LINKER_LAUNCHER=%EI_STEP_WRAPPER%" || exit /b 1
rem A build whose failed steps printed no error (or the linker's LNK1000) is run once more (tools/build_retry.py).
python "%~dp0tools\build_retry.py" "%OUT%" %TARGET% || exit /b 1
if /i "%CFG%"=="coverage" (echo Built: %OUT%& exit /b 0)
rem The checks (tools/build_checks.py, side by side): the cleanup and comment counts, the data seam, the harness key,
rem the generated and listed files, the unit checks (ei_unit_check), the interface canvases (ei_ui_check) and the
rem performance overlay (ei_perf_check).
python "%~dp0tools\build_gate.py" stage checks
python "%~dp0tools\build_checks.py" "%OUT%" || exit /b 1
echo Built: %OUT%
