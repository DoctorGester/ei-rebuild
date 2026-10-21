cmake_minimum_required(VERSION 3.24)
project(EvilIslands CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build for 32-bit x86 (game.exe and its DLLs are 32-bit). Use the x86 vcvars environment.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# No incremental linking in the two builds: an incremental relink of ei_hook.dll twice left out new code
# from ei_render.lib while reporting success (R16c2), so a run could test stale code.
foreach(kind EXE SHARED MODULE)
    string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "" flags "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO "${flags} /INCREMENTAL:NO")
endforeach()
# Inline expansion (R16c5): RelWithDebInfo's /Ob1 inlines only functions marked inline; /Ob2 also the
# compiler's own picks (file-local helpers). EI_INLINE=1 builds as before, for A/B timing runs.
set(EI_INLINE 2 CACHE STRING "Inline expansion level /Ob<n> of the release and verify builds")
string(REGEX REPLACE "/Ob[0-3]" "/Ob${EI_INLINE}" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

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
# The renderer's shaders: written in src/render/shaders/*.hlsl, compiled to bytecode here with the Windows
# SDK's fxc (on the path in the VS build environment) into headers under shaders/ in the build folder, which
# render_shaders.cpp includes; sokol takes the bytecode, so no shader compiler runs at start (SV10b). The
# shaders include each other: each header depends on all of them.
find_program(EI_FXC fxc REQUIRED)
file(GLOB EI_SHADER_SOURCES CONFIGURE_DEPENDS src/render/shaders/*.hlsl src/render/shaders/*.hlsli)
set(EI_SHADER_SCRIPT ${CMAKE_SOURCE_DIR}/src/render/shaders/compile.cmake)
set(EI_SHADER_HEADERS)
# ei_shader(<array> <file> <entry> <profile> [<define>...]): the array's header shaders/<array>.h.
function(ei_shader name file entry profile)
    set(out ${CMAKE_BINARY_DIR}/shaders/${name}.h)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DFXC=${EI_FXC} -DSOURCE=${CMAKE_SOURCE_DIR}/src/render/shaders/${file} -DENTRY=${entry}
                -DPROFILE=${profile} -DNAME=${name} "-DDEFINES=${ARGN}" -DOUT=${out} -P ${EI_SHADER_SCRIPT}
        DEPENDS ${EI_SHADER_SOURCES} ${EI_SHADER_SCRIPT}
        COMMENT "Compiling shader ${file} ${entry} ${ARGN}"
        VERBATIM)
    set(EI_SHADER_HEADERS ${EI_SHADER_HEADERS} ${out} PARENT_SCOPE)
endfunction()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
foreach(s blit clear_depth mesh terrain present shadow_caster into_multisampled)
    ei_shader(${s}_vs ${s}.hlsl vs_main vs_4_0)
    ei_shader(${s}_ps ${s}.hlsl ps_main ps_4_0)
endforeach()
ei_shader(depth_from_multisampled_vs depth_from_multisampled.hlsl vs_main vs_4_0 SAMPLES=8)   # (reads no texture)
foreach(samples 2 4 8)   # render_internal.h's kSmoothSamples and the counts below it
    ei_shader(depth_from_multisampled${samples}_ps depth_from_multisampled.hlsl ps_main ps_4_0 SAMPLES=${samples})
endforeach()
ei_shader(shadow_range_vs shadow_range_vs.hlsl vs_main vs_4_0)
ei_shader(shadow_reduce_ps shadow_reduce.hlsl ps_main ps_4_1)   # Gather (Direct3D 10.1)
ei_shader(shadow_dilate_ps shadow_dilate.hlsl ps_main ps_4_1)
# The interface canvases (render_ui.cpp): one file, an entry point per program.
ei_shader(ui_rect_vs ui_canvas.hlsl vs_rect vs_4_0)
ei_shader(ui_glyph_vs ui_canvas.hlsl vs_glyph vs_4_0)
foreach(e solid glyph compose grey copy blit)
    ei_shader(ui_${e}_ps ui_canvas.hlsl ps_${e} ps_4_0)
endforeach()
target_sources(ei_render PRIVATE ${EI_SHADER_HEADERS})
target_include_directories(ei_render PRIVATE ${CMAKE_BINARY_DIR})

add_library(ei_hook SHARED
    src/hook/cover.cpp
    src/hook/dllmain.cpp
    src/hook/hook.cpp
    src/hook/paths.cpp
    src/hook/profile.cpp
    src/hook/rebirth_hook.cpp
    src/hook/standalone.cpp
    src/standalone/image_code.cpp
    src/standalone/image_data.cpp
    src/standalone/image_imports.cpp
    src/standalone/static_exit.cpp
    src/standalone/static_init.cpp
    src/standalone/static_init_run.cpp
    ${EI_GAME_SOURCES})
target_include_directories(ei_hook PRIVATE include src)
target_link_libraries(ei_hook PRIVATE ei_render)
# game.exe's .data bytes for the image's own copy of the section (src/standalone/image_data.cpp, S18d), written
# from game.exe at build time into image/ in the build folder (not committed; tools/gen_image_cells.py --bytes).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(EI_GAME_EXE "" CACHE FILEPATH "game.exe 1.06, the source of the image's data (empty: tools/exe_path.py finds it, like every tool that reads game.exe)")
if(NOT EI_GAME_EXE)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/exe_path.py
                    OUTPUT_VARIABLE EI_GAME_EXE_FOUND ERROR_VARIABLE EI_GAME_EXE_ERROR RESULT_VARIABLE EI_GAME_EXE_RESULT
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT EI_GAME_EXE_RESULT EQUAL 0)
        message(FATAL_ERROR "${EI_GAME_EXE_ERROR}")
    endif()
    file(TO_CMAKE_PATH "${EI_GAME_EXE_FOUND}" EI_GAME_EXE)
endif()
# The dispatch-only functions' bytes (InstallHooks' check of the EI_REPLACE_DISPATCH thunks, S18k) come the same way.
set(EI_IMAGE_DATA ${CMAKE_BINARY_DIR}/image/image_data_bytes.inc)
set(EI_IMAGE_DISPATCH ${CMAKE_BINARY_DIR}/image/dispatch_bytes.inc)
add_custom_command(OUTPUT ${EI_IMAGE_DATA} ${EI_IMAGE_DISPATCH}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_image_cells.py --exe ${EI_GAME_EXE} --bytes ${EI_IMAGE_DATA}
            --dispatch ${EI_IMAGE_DISPATCH}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_image_cells.py ${CMAKE_SOURCE_DIR}/config/named_data.csv ${EI_GAME_EXE}
    COMMENT "Writing game.exe's .data bytes"
    VERBATIM)
target_sources(ei_hook PRIVATE ${EI_IMAGE_DATA} ${EI_IMAGE_DISPATCH})
set_source_files_properties(src/standalone/image_data.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/image
                            OBJECT_DEPENDS ${EI_IMAGE_DATA})
set_source_files_properties(src/hook/hook.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/image
                            OBJECT_DEPENDS ${EI_IMAGE_DISPATCH})
if(EI_HARNESS)
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp)
    # Function vector record mode (tests/vectors/vectors.h, EI_VECTORS=1 in game.exe).
    file(GLOB EI_RECORD_SOURCES CONFIGURE_DEPENDS tests/vectors/record_*.cpp)
    target_sources(ei_hook PRIVATE ${EI_HARNESS_SOURCES} ${EI_RECORD_SOURCES} tests/vectors/vectors_record.cpp)
endif()
# The NaN audit's log (src/lib/nan_audit.h) in the verify and debug builds.
target_compile_definitions(ei_hook PRIVATE $<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)
# Hooks register themselves via static objects; keep them even if nothing references them.
# /MAP: ei_hook.map lets the harness crash log (ei_hook+offset) be resolved to functions.
target_link_options(ei_hook PRIVATE /OPT:NOREF /MAP)

add_executable(ei_launch tools/launcher/launch.cpp)

# The standalone host (docs/notes/standalone.md section 2): an ordinary exe (S18k: no fixed base, no read of game.exe)
# that reserves what of game.exe's range is free (the guard) and runs ei_hook.dll's code for it.
# game.exe's resources (icons, cursors, bitmaps, dialogs, strings) are the host's own (S18j): a .res written from
# game.exe at build time (tools/gen_resources.py, not committed) that the linker builds in.
set(EI_IMAGE_RESOURCES ${CMAKE_BINARY_DIR}/image/game_resources.res)
add_custom_command(OUTPUT ${EI_IMAGE_RESOURCES}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_resources.py --exe ${EI_GAME_EXE} --res ${EI_IMAGE_RESOURCES}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_resources.py ${EI_GAME_EXE}
    COMMENT "Writing game.exe's resources"
    VERBATIM)
set_source_files_properties(${EI_IMAGE_RESOURCES} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_executable(ei_standalone src/standalone/host.cpp ${EI_IMAGE_RESOURCES})
target_include_directories(ei_standalone PRIVATE src)

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
    src/lib/mfc_coll.cpp
    src/lib/mfc_file.cpp
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
# Offline check of the interface canvases (render_ui, ticket N1) on a WARP device: every command against
# a CPU reference, and fills, frames, lines and pixels against GDI drawing into a DIB section (GDI only there).
add_executable(ei_ui_check tools/ui_check/ui_check.cpp)
target_include_directories(ei_ui_check PRIVATE include src third_party/sokol)
target_link_libraries(ei_ui_check PRIVATE ei_render gdi32 user32)
target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)

