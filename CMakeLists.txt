cmake_minimum_required(VERSION 3.24)
project(EvilIslands CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build for 32-bit x86 (game.exe and its DLLs are 32-bit). Use the x86 vcvars environment.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(EI_VERIFY "Build ei_hook with differential verification wrappers" OFF)

# x87 floating point, as in the original: SSE rounds differently and would change behaviour.
# /fp:fast matches VC6's default x87 codegen (values stay in 80-bit registers, returns are not
# re-rounded to float); /fp:precise inserts extra rounding stores the original never did.
add_compile_options(/arch:IA32 /fp:fast /W4 /permissive- /utf-8)

file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)

# The new renderer (docs/notes/renderer.md): sokol_gfx on Direct3D 11, switched on with EI_RENDER.
# The verify build turns on sokol's validation layer (messages go to ei_hook.log).
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES})
target_include_directories(ei_render PRIVATE include src third_party/sokol)
target_link_libraries(ei_render PUBLIC d3d11 dxgi)
if(EI_VERIFY)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
endif()

add_library(ei_hook SHARED
    src/hook/cover.cpp
    src/hook/dllmain.cpp
    src/hook/hook.cpp
    src/hook/paths.cpp
    src/hook/rebirth_hook.cpp
    ${EI_GAME_SOURCES})
target_include_directories(ei_hook PRIVATE include src)
target_link_libraries(ei_hook PRIVATE ei_render)
if(EI_VERIFY)
    file(GLOB EI_VERIFY_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp)
    target_sources(ei_hook PRIVATE ${EI_VERIFY_SOURCES})
endif()
# Hooks register themselves via static objects; keep them even if nothing references them.
# /MAP: ei_hook.map lets the harness crash log (ei_hook+offset) be resolved to functions.
target_link_options(ei_hook PRIVATE /OPT:NOREF /MAP)

add_executable(ei_launch tools/launcher/launch.cpp)
target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)

# Functions already verified in game run without their verify wrappers; disabled ones are not hooked.
add_custom_command(TARGET ei_hook POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/tests/verify/verified.txt $<TARGET_FILE_DIR:ei_hook>/verified.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/tests/verify/disabled.txt $<TARGET_FILE_DIR:ei_hook>/disabled.txt)
