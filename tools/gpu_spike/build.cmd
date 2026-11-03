@echo off
rem The SDL_GPU spike's build (tools/gpu_spike/README.md), apart from build.cmd's: the verify build with the spike's
rem recorder and ei_gpu_spike into build\gpu_spike, then the timing build into build\gpu_spike_release.
setlocal
if defined EI_VSROOT (set "VSROOT=%EI_VSROOT%") else (set "VSROOT=E:\bin\vs\buildtools")
if not defined EI_SHDC set "EI_SHDC=E:\bin\sokol-shdc\sokol-shdc.exe"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set "ROOT=%~dp0..\.."
set "_MSPDBSRV_ENDPOINT_=ei_gpu_spike"
cmake -S "%ROOT%" -B "%ROOT%\build\gpu_spike" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=ON -DEI_GPU_SPIKE=ON "-DEI_SHDC=%EI_SHDC%" || exit /b 1
cmake --build "%ROOT%\build\gpu_spike" %* || exit /b 1
rem The timing build: ei_gpu_spike without the harness, so sokol's validation layer is off as in the product.
cmake -S "%ROOT%" -B "%ROOT%\build\gpu_spike_release" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=OFF -DEI_GPU_SPIKE=ON "-DEI_SHDC=%EI_SHDC%" || exit /b 1
cmake --build "%ROOT%\build\gpu_spike_release" --target ei_gpu_spike || exit /b 1
echo Built: %ROOT%\build\gpu_spike (recording, with the harness) and %ROOT%\build\gpu_spike_release (timing)
