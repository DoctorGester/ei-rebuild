@echo off
rem Builds ei_hook.dll and ei_launch.exe (32-bit) with the VS 2022 Build Tools.
rem usage: build.cmd [release|verify|debug]
setlocal
set VSROOT=E:\bin\vs\buildtools
set CFG=%~1
if "%CFG%"=="" set CFG=release

call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

set VERIFY=OFF
set BUILDTYPE=RelWithDebInfo
if /i "%CFG%"=="verify" set VERIFY=ON
if /i "%CFG%"=="debug" set BUILDTYPE=Debug

cmake -S "%~dp0." -B "%~dp0build\%CFG%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DEI_VERIFY=%VERIFY% || exit /b 1
cmake --build "%~dp0build\%CFG%" || exit /b 1
rem Hooks registered through a virtual-call thunk recurse forever (rewrite-guide rule 4).
python "%~dp0tools\check_thunks.py" || exit /b 1
rem Replacements declared void whose game callers use EAX afterwards (UnitActionPoint, AddSurface).
python "%~dp0tools\check_returns.py" --map "%~dp0build\%CFG%\ei_hook.map" || exit /b 1
rem New duplicate EI_ORIGINAL forwarders: use the shared src/game/originals_<area>.h (ticket C3).
python "%~dp0tools\check_forwarders.py" || exit /b 1
rem Cleanup-plan patterns (offsets, VCall, __asm, raw addresses) may only go down per file.
python "%~dp0tools\check_cleanup.py" || exit /b 1
echo Built: %~dp0build\%CFG%
