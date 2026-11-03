cmake_minimum_required(VERSION 3.24)
# The shader toolchain's build (toolchain.cmake runs it once at configure time): SDL_shadercross's command-line tool
# and SPIRV-Cross's (its --reflect output feeds tools/gen_gpu_shaders.py), from the sources toolchain.cmake fetched,
# with our SDL (EI_SDL) and DirectXShaderCompiler's release binaries (EI_DXC). Everything is linked statically but
# DXC, whose dxcompiler and dxil sit beside shadercross (Windows) or are found through its run path (Linux).
project(ei_shader_toolchain C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_RENDER SDL_AUDIO
            SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(${EI_SDL} sdl EXCLUDE_FROM_ALL SYSTEM)

set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)   # its install export set lets shadercross export its own
add_subdirectory(${EI_SPIRV_CROSS} spirv-cross EXCLUDE_FROM_ALL SYSTEM)
# shadercross looks for this name before its find_package; the static target it links is spirv-cross-c.
add_library(spirv_cross_c INTERFACE)

if(WIN32)
    set(DirectXShaderCompiler_INCLUDE_PATH ${EI_DXC}/inc CACHE PATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_BINARY ${EI_DXC}/bin/x64/dxcompiler.dll CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_LIBRARY ${EI_DXC}/lib/x64/dxcompiler.lib CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxil_BINARY ${EI_DXC}/bin/x64/dxil.dll CACHE FILEPATH "" FORCE)
else()
    set(DirectXShaderCompiler_INCLUDE_PATH ${EI_DXC}/include/dxc CACHE PATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_LIBRARY ${EI_DXC}/lib/libdxcompiler.so CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxil_LIBRARY ${EI_DXC}/lib/libdxil.so CACHE FILEPATH "" FORCE)
endif()
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${EI_SHADERCROSS_SOURCE} shadercross)
if(WIN32)
    add_custom_target(shader_toolchain ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EI_DXC}/bin/x64/dxcompiler.dll ${EI_DXC}/bin/x64/dxil.dll
                $<TARGET_FILE_DIR:shadercross>
        DEPENDS shadercross spirv-cross)
else()
    add_custom_target(shader_toolchain ALL DEPENDS shadercross spirv-cross)
    set_target_properties(shadercross PROPERTIES BUILD_RPATH ${EI_DXC}/lib)
endif()
