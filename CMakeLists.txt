cmake_minimum_required(VERSION 3.24)
project(EvilIslands CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build for 32-bit x86 (game.exe and its DLLs are 32-bit). Use the x86 vcvars environment.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Two builds (build.cmd): `release` is the product, the standalone image (ei_standalone.exe with ei_hook.dll's
# code); `verify` is the harness build, the same image and code plus the harness (tests/verify: scenario runs
# of the image, and of the original game.exe with EI_NO_HOOKS for goldens) and the vector recorders
# (tests/vectors/record_*.cpp, installed only in the original game.exe with EI_VECTORS=1).
option(EI_HARNESS "Build ei_hook with the harness and the vector recorders (tests/verify, tests/vectors)" OFF)

# x87 floating point, as in the original: SSE rounds differently and would change behaviour.
# /fp:precise (SV7d1a): /fp:fast regrouped sums and folded constants across them, which drifted by
# an ulp (SV7d1). Precise's extra rounding stores change nothing on the main thread, which runs
# at 24-bit precision; it calls the CRT for sqrt/sin/cos/atan2/exp/log, so game code uses x87::.
add_compile_options(/arch:IA32 /fp:precise /W4 /permissive- /utf-8)

file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)

# The new renderer (docs/notes/renderer.md): sokol_gfx on Direct3D 11, switched on with EI_RENDER.
# The harness build turns on sokol's validation layer (messages go to ei_hook.log).
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES})
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol)
target_link_libraries(ei_render PUBLIC d3d11 dxgi)
# Interface text (render_text.cpp): the fonts' default folder while developing; a host can change it.
target_compile_definitions(ei_render PRIVATE EI_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")
if(EI_HARNESS)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
endif()

add_library(ei_hook SHARED
    src/hook/cover.cpp
    src/hook/dllmain.cpp
    src/hook/hook.cpp
    src/hook/paths.cpp
    src/hook/profile.cpp
    src/hook/rebirth_hook.cpp
    src/hook/standalone.cpp
    src/standalone/static_exit.cpp
    src/standalone/static_init.cpp
    src/standalone/static_init_run.cpp
    ${EI_GAME_SOURCES})
target_include_directories(ei_hook PRIVATE include src)
target_link_libraries(ei_hook PRIVATE ei_render)
if(EI_HARNESS)
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp)
    # Function vector record mode (tests/vectors/vectors.h, EI_VECTORS=1 in game.exe).
    file(GLOB EI_RECORD_SOURCES CONFIGURE_DEPENDS tests/vectors/record_*.cpp)
    target_sources(ei_hook PRIVATE ${EI_HARNESS_SOURCES} ${EI_RECORD_SOURCES} tests/vectors/vectors_record.cpp)
endif()
# Hooks register themselves via static objects; keep them even if nothing references them.
# /MAP: ei_hook.map lets the harness crash log (ei_hook+offset) be resolved to functions.
target_link_options(ei_hook PRIVATE /OPT:NOREF /MAP)

add_executable(ei_launch tools/launcher/launch.cpp)

# The standalone host (docs/notes/standalone.md section 2): game.exe's data laid out at 0x400000 inside
# its own image, game.exe's code replaced by ei_hook.dll's jumps. Linked at a fixed base below that range.
add_executable(ei_standalone src/standalone/host.cpp)
target_link_options(ei_standalone PRIVATE /BASE:0x200000 /FIXED /DYNAMICBASE:NO)

# Replays the recorded function vectors (tests/vectors/vectors.h) against our code without the game:
# the families' sources, their checks, and a stub for the hook registrations. build.cmd runs it.
file(GLOB EI_REPLAY_SOURCES CONFIGURE_DEPENDS tests/vectors/replay_*.cpp)
add_executable(ei_vectors
    ${EI_REPLAY_SOURCES}
    tests/vectors/hook_stub.cpp
    src/game/ai_block_search.cpp
    src/game/ai_grids.cpp
    src/game/ai_join.cpp
    src/game/ai_local_search.cpp
    src/game/ai_map.cpp
    src/game/ai_motion.cpp
    src/game/ai_movement.cpp
    src/game/ai_path.cpp
    src/game/ai_path_cells.cpp
    src/game/ai_path_smooth.cpp
    src/game/ai_plan.cpp
    src/game/ai_region_reach.cpp
    src/game/core_maths.cpp
    src/game/core_refcount.cpp
    src/game/game_arrays.cpp
    src/game/game_buffer.cpp
    src/game/game_lists.cpp
    src/game/maths.cpp
    src/game/matrix_math.cpp
    src/game/net_coder.cpp
    src/game/quaternion.cpp
    src/game/random.cpp
    src/game/record_arrays.cpp
    src/game/refptr_copies.cpp
    src/game/script_arrays.cpp
    src/game/script_blocks.cpp
    src/game/script_copy.cpp
    src/game/script_destroy.cpp
    src/game/script_group.cpp
    src/game/script_load.cpp
    src/game/script_parse.cpp
    src/game/script_parse_arrays.cpp
    src/game/script_parse_load.cpp
    src/game/script_reset.cpp
    src/game/script_runtime_helpers.cpp
    src/game/script_vars.cpp
    src/game/script_vars_append.cpp
    src/game/unit_hit_location.cpp
    src/game/unit_params_protection.cpp
    src/game/unit_params_skill.cpp
    src/game/unit_skill_price.cpp
    src/game/unit_stats.cpp
    src/lib/crt.cpp
    src/lib/crt_format.cpp
    src/lib/mfc.cpp
    src/lib/mfc_archive.cpp
    src/lib/mfc_array.cpp
    src/lib/mfc_list.cpp
    src/lib/mfc_string.cpp
    src/lib/rebirth.cpp
    src/lib/stlp.cpp
    src/lib/stlp_string.cpp)
target_include_directories(ei_vectors PRIVATE include src)
# EI_VECTORS_REPLAY: the game's data at fixed addresses comes from the checks' stand-in (src/game/game_data.h).
target_compile_definitions(ei_vectors PRIVATE EI_VECTORS_DATA="${CMAKE_SOURCE_DIR}/tests/vectors/data" EI_VECTORS_REPLAY)

# Offline check of the interface text layout (render_text): DrawText rules on synthetic cases, and a
# report against GDI on the game's strings (tools/text_check; GDI only there).
add_executable(ei_text_check tools/text_check/text_check.cpp)
target_include_directories(ei_text_check PRIVATE src)
target_link_libraries(ei_text_check PRIVATE ei_render gdi32 user32 shell32)
target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)

