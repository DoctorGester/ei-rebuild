cmake_minimum_required(VERSION 3.24)
# SDL_shadercross's command line tool for the SDL_GPU spike (tools/gpu_spike/README.md), built from the sources
# fetch_third_party.py put under build/third_party: our SDL (third_party/SDL, with its GPU API), SPIRV-Cross and
# DirectXShaderCompiler's release binaries, all linked statically but DXC (dxcompiler / dxil beside the exe).
# cmake -S tools/gpu_spike/shadercross -B build/third_party/shadercross-build -G Ninja -DEI_THIRD_PARTY=<build/third_party>
project(ei_shadercross C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
get_filename_component(EI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_RENDER SDL_AUDIO
            SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(${EI_ROOT}/third_party/SDL sdl EXCLUDE_FROM_ALL SYSTEM)

set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)   # its install export set lets shadercross export its own
add_subdirectory(${EI_THIRD_PARTY}/SPIRV-Cross-vulkan-sdk-1.4.363.0 spirv-cross EXCLUDE_FROM_ALL SYSTEM)
# shadercross looks for this name before its find_package; the static target it links is spirv-cross-c.
add_library(spirv_cross_c INTERFACE)

if(WIN32)
    set(DXC ${EI_THIRD_PARTY}/dxc)
    set(DirectXShaderCompiler_INCLUDE_PATH ${DXC}/inc CACHE PATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_BINARY ${DXC}/bin/x64/dxcompiler.dll CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_LIBRARY ${DXC}/lib/x64/dxcompiler.lib CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxil_BINARY ${DXC}/bin/x64/dxil.dll CACHE FILEPATH "" FORCE)
else()
    set(DXC ${EI_THIRD_PARTY}/dxc-linux)
    set(DirectXShaderCompiler_INCLUDE_PATH ${DXC}/include/dxc CACHE PATH "" FORCE)
    set(DirectXShaderCompiler_dxcompiler_LIBRARY ${DXC}/lib/libdxcompiler.so CACHE FILEPATH "" FORCE)
    set(DirectXShaderCompiler_dxil_LIBRARY ${DXC}/lib/libdxil.so CACHE FILEPATH "" FORCE)
endif()
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${EI_THIRD_PARTY}/shadercross_src/SDL_shadercross-1ff05bec573988a98ef9e0260b4da44f512b8367 shadercross)
if(WIN32)
    add_custom_target(shadercross_tool ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DXC}/bin/x64/dxcompiler.dll ${DXC}/bin/x64/dxil.dll
                $<TARGET_FILE_DIR:shadercross>
        DEPENDS shadercross)
else()
    add_custom_target(shadercross_tool ALL DEPENDS shadercross)
    set_target_properties(shadercross PROPERTIES BUILD_RPATH ${DXC}/lib)
endif()
