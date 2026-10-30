cmake_minimum_required(VERSION 3.24)
project(EvilIslands CXX RC)

# 32-bit x86 (the vcvars32 environment) or x64 (vcvars64: build.cmd x64 / verify-x64). The function vectors' replay
# (tests/vectors) is 32-bit only; the harness builds for both (build.cmd verify-x64).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(EI_X64 ON)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(EI_X64 OFF)
else()
    message(FATAL_ERROR "Build for 32-bit x86 or x64: use the x86 or x64 vcvars environment.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The debug build's containers are the release build's: the checked iterators' proxy is allocated by the
# constructor, so a constinit std::vector global (the game-order exit functions' containers) fails to compile.
add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
# No incremental linking in the two builds: an incremental relink of the game's DLL twice left out new code
# from ei_render.lib while reporting success (R16c2), so a run could test stale code.
foreach(kind EXE SHARED MODULE)
    string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "" flags "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO "${flags} /INCREMENTAL:NO")
endforeach()
# Inline expansion (R16c5): RelWithDebInfo's /Ob1 inlines only functions marked inline; /Ob2 also the
# compiler's own picks (file-local helpers). EI_INLINE=1 builds as before, for A/B timing runs.
set(EI_INLINE 2 CACHE STRING "Inline expansion level /Ob<n> of the release and verify builds")
string(REGEX REPLACE "/Ob[0-3]" "/Ob${EI_INLINE}" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# Two builds (build.cmd): `release` is the product, the standalone image (ei-rebuild.exe, the whole game); `verify`
# is the harness build, the same image with the harness linked in (tests/verify: scenario runs of the image). Until H40
# it also built ei_harness.dll, the harness in the original game.exe (the tag original-oracle). On x64 (build.cmd
# verify-x64) that build's ei_launch starts the x64 image (run_all.py --x64).
option(EI_HARNESS "Build the harness, linked into ei-rebuild.exe" OFF)

# SDL3 (third_party/SDL; docs/notes/platform.md, "SDL3"): the window, its events, the cursor, the mouse, the clipboard,
# the message box, the clocks and the threads of the builds without the harness (src/platform/sdl), and the sound
# device of every build (src/platform/audio_sdl.cpp). Linked statically, so the exe stays one file dropped into a game
# folder (S23), with only the subsystems those use. Added before the game's compile options: its warnings are its own.
foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_GPU SDL_RENDER SDL_CAMERA
            SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG SDL_OPENGL SDL_OPENGLES SDL_VULKAN
            SDL_OFFSCREEN SDL_DISKAUDIO SDL_DUMMYAUDIO SDL_DUMMYVIDEO)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL SYSTEM)

# Every string inside the game is UTF-8 (docs/notes/standalone.md section 4, src/game/asset_text.h): asset, save and
# network text is converted from and to EI_ASSET_CODEPAGE where it crosses.
set(EI_ASSET_CODEPAGE 1251 CACHE STRING "The code page of the assets', the saves' and the network's text")
add_compile_definitions(EI_ASSET_CODEPAGE=${EI_ASSET_CODEPAGE})

# SSE2 floating point everywhere: the original's x87 maths at the main thread's 24-bit precision is float maths in
# its order, with src/lib/pc24.h where an operand was wider (docs/notes/cleanup-tooling.md, "The x87 files to SSE2").
# /fp:precise (SV7d1a): /fp:fast regrouped sums and folded constants across them, which drifted by an ulp (SV7d1).
add_compile_options(/fp:precise /W4 /permissive- /utf-8)
if(NOT EI_X64)
    add_compile_options(/arch:SSE2)   # x64 has SSE2 always
endif()

# AddressSanitizer (build.cmd asan: the verify build with it, into build\asan; docs/notes/harness.md): every target
# instrumented (the STL's container annotations must match across the libraries), the runtime DLL copied beside the
# executables. Its reports go to the run's folder (tests/verify/image/asan_report.cpp).
option(EI_ASAN "Build with AddressSanitizer (/fsanitize=address)" OFF)
if(EI_ASAN)
    add_compile_options(/fsanitize=address)
    get_filename_component(EI_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(COPY "${EI_CL_DIR}/clang_rt.asan_dynamic-i386.dll" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# The game code.
file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)

# The new renderer (docs/notes/renderer.md): sokol_gfx on Direct3D 11, switched on with EI_RENDER.
# The harness build turns on sokol's validation layer (messages go to ei-rebuild.log).
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES})
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol)
target_link_libraries(ei_render PUBLIC d3d11 dxgi)
if(EI_HARNESS)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
endif()

# The platform layer (docs/notes/platform.md): the game's calls of the operating system (src/platform/platform.h). The
# window, its events, the cursor, the mouse, the clipboard, the message box, the clocks and the threads on SDL3
# (src/platform/sdl), or in the harness's builds on Windows' window procedure and game.exe's import slots, which the
# harness drives (src/platform/win32/platform_win32_window.cpp); the rest on Windows (src/platform/win32, with game.exe's
# imports, gameapi.h), the one place of the game's code that includes <windows.h>; the sound on SDL3's audio device
# with the dr_wav / dr_mp3 decoders (src/platform/audio_sdl.cpp); the movies on the Bink decoder in src/platform/bink.
file(GLOB EI_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/*.cpp src/platform/win32/*.cpp src/platform/bink/*.cpp)
list(REMOVE_ITEM EI_PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/win32/platform_win32_window.cpp)
if(EI_HARNESS)
    list(APPEND EI_PLATFORM_SOURCES src/platform/win32/platform_win32_window.cpp)
else()
    file(GLOB EI_PLATFORM_SDL_SOURCES CONFIGURE_DEPENDS src/platform/sdl/*.cpp)
    list(APPEND EI_PLATFORM_SOURCES ${EI_PLATFORM_SDL_SOURCES})
endif()
add_library(ei_platform STATIC ${EI_PLATFORM_SOURCES})
target_include_directories(ei_platform PRIVATE include src third_party)
target_link_libraries(ei_platform PRIVATE SDL3::SDL3-static)
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
foreach(s blit solid mesh terrain present shadow_caster shadow_terrain into_multisampled perf_overlay texture_compose)
    ei_shader(${s}_vs ${s}.hlsl vs_main vs_4_0)
    ei_shader(${s}_ps ${s}.hlsl ps_main ps_4_0)
endforeach()
ei_shader(depth_from_multisampled_vs depth_from_multisampled.hlsl vs_main vs_4_0 SAMPLES=8)   # (reads no texture)
foreach(samples 2 4 8)   # render_internal.h's kSmoothSamples and the counts below it
    ei_shader(depth_from_multisampled${samples}_ps depth_from_multisampled.hlsl ps_main ps_4_0 SAMPLES=${samples})
endforeach()
# The global map's clouds (render_compose.cpp): a frame of noise and the blend of two.
ei_shader(texture_clouds_vs texture_clouds.hlsl vs_main vs_4_0)
ei_shader(texture_clouds_frame_ps texture_clouds.hlsl ps_frame ps_4_0)
ei_shader(texture_clouds_ps texture_clouds.hlsl ps_clouds ps_4_0)
ei_shader(shadow_range_vs shadow_range_vs.hlsl vs_main vs_4_0)
ei_shader(shadow_reduce_ps shadow_reduce.hlsl ps_main ps_4_1)   # Gather (Direct3D 10.1)
ei_shader(shadow_dilate_ps shadow_dilate.hlsl ps_main ps_4_1)
# The interface canvases (render_ui.cpp): one file, an entry point per program.
ei_shader(ui_rect_vs ui_canvas.hlsl vs_rect vs_4_0)
ei_shader(ui_glyph_vs ui_canvas.hlsl vs_glyph vs_4_0)
foreach(e solid glyph compose grey copy rgb565 blit)
    ei_shader(ui_${e}_ps ui_canvas.hlsl ps_${e} ps_4_0)
endforeach()
target_sources(ei_render PRIVATE ${EI_SHADER_HEADERS})
target_include_directories(ei_render PRIVATE ${CMAKE_BINARY_DIR})
# The interface fonts built into the executable (render_fonts.cpp): third_party/fonts' two Liberation files written
# as byte arrays into fonts/ in the build folder at build time (src/render/embed.cmake; not committed).
set(EI_FONT_HEADERS)
foreach(font LiberationSerif LiberationMono)
    set(out ${CMAKE_BINARY_DIR}/fonts/${font}.h)
    set(ttf ${CMAKE_SOURCE_DIR}/third_party/fonts/${font}-Regular.ttf)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${ttf} -DNAME=k${font} -DOUT=${out} -P ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
        DEPENDS ${ttf} ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
        COMMENT "Writing ${font}-Regular.ttf as bytes"
        VERBATIM)
    list(APPEND EI_FONT_HEADERS ${out})
endforeach()
target_sources(ei_render PRIVATE ${EI_FONT_HEADERS})
# The product's notices (third_party_notices.txt beside the executables; play.cmd copies it): the licences of the
# third-party code and fonts built in, the fonts' SIL Open Font License among them.
set(EI_NOTICES)
foreach(part fonts/README.txt fonts/LICENSE SDL/README.txt SDL/LICENSE.txt dr_libs/README.txt dr_libs/LICENSE sokol/README.txt sokol/LICENSE
             stb/README.txt)
    file(READ ${CMAKE_SOURCE_DIR}/third_party/${part} text)
    string(APPEND EI_NOTICES "==== third_party/${part}\n\n${text}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/third_party/${part})
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/third_party_notices.txt
     "Third-party software built into ei-rebuild.exe, with its licences.\n\n${EI_NOTICES}")

# The game (docs/notes/standalone.md section 2): ei-rebuild.exe, an ordinary exe (S18k: ASLR as any exe, no read of
# game.exe) with the whole game linked in (S23: no DLL; dropped into a game folder it plays from there). Its one resource
# is its version information (src/standalone/host.rc, C23): game.exe's own resources are not used. A windowed program
# (no console) whose entry is main.
add_executable(ei-rebuild
    src/hook/log.cpp
    src/hook/profile.cpp
    src/hook/rebirth_hook.cpp
    src/hook/standalone.cpp
    src/standalone/host.cpp
    src/standalone/host.rc
    src/standalone/image_imports.cpp
    src/standalone/standalone_main.cpp
    src/standalone/startup.cpp
    src/standalone/static_exit.cpp
    src/standalone/static_init.cpp)
set_target_properties(ei-rebuild PROPERTIES WIN32_EXECUTABLE ON)
target_include_directories(ei-rebuild PRIVATE include src)
target_link_libraries(ei-rebuild PRIVATE ei_render ei_platform)
target_link_options(ei-rebuild PRIVATE /ENTRY:mainCRTStartup)
if(EI_ASAN)
    # ASan's shadow, redzones and quarantine take hundreds of MB: 4 GB of address space, not 2.
    target_link_options(ei-rebuild PRIVATE /LARGEADDRESSAWARE)
else()
    # The exe's own imports (Direct3D 11, DXGI, ...) from System32 only: a game folder may hold a wrapper of the same
    # name (dgVoodoo's DLLs beside the original's game.exe). game.exe's imports load from the game folder on purpose
    # (image_imports.cpp: Miles, Bink). ASan's runtime is beside the exe, so its build searches as usual.
    target_link_options(ei-rebuild PRIVATE /DEPENDENTLOADFLAG:0x800)
endif()
# The Rebirth install's profile, which an exe takes in a Rebirth folder (src/standalone/startup.cpp):
# config/rebirth_profile.ini's patches as a spec (rebirth::ApplySpec: none, then each name), written at configure time
# into config/rebirth_profile.inc in the build folder.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/config/rebirth_profile.ini)
file(STRINGS ${CMAKE_SOURCE_DIR}/config/rebirth_profile.ini EI_REBIRTH_PROFILE_LINES REGEX "^[A-Za-z]+ = true$")
set(EI_REBIRTH_PROFILE none)
foreach(line ${EI_REBIRTH_PROFILE_LINES})
    string(REGEX REPLACE " = true$" "" name ${line})
    list(APPEND EI_REBIRTH_PROFILE ${name})
endforeach()
list(JOIN EI_REBIRTH_PROFILE "," EI_REBIRTH_PROFILE)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/config/rebirth_profile.inc CONTENT "\"@EI_REBIRTH_PROFILE@\"\n" @ONLY)
set_source_files_properties(src/standalone/startup.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/config)
if(EI_HARNESS)
    # The harness (tests/verify; docs/notes/harness.md): its shared part, with what it names of the hook framework's
    # game.exe side (src/hook/hook.cpp, cover.cpp) and of the vector recordings (vectors_record.cpp: the report
    # flushes them), linked into this exe with the image's side (tests/verify/image, which calls our code).
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp)
    add_library(ei_harness_shared OBJECT
        ${EI_HARNESS_SOURCES} src/hook/hook.cpp src/hook/cover.cpp tests/vectors/vectors_record.cpp)
    target_include_directories(ei_harness_shared PRIVATE include src)
    file(GLOB EI_HARNESS_IMAGE_SOURCES CONFIGURE_DEPENDS tests/verify/image/*.cpp)
    target_sources(ei-rebuild PRIVATE $<TARGET_OBJECTS:ei_harness_shared> ${EI_HARNESS_IMAGE_SOURCES})
endif()
# The game code: a library, whole (every object, as if listed: the static objects' initialisers too).
add_library(ei_game STATIC ${EI_GAME_SOURCES})
target_include_directories(ei_game PRIVATE include src)
target_link_libraries(ei-rebuild PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
# The NaN audit's log (src/lib/nan_audit.h) in the verify and debug builds.
target_compile_definitions(ei-rebuild PRIVATE $<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)
target_compile_definitions(ei_game PRIVATE $<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)
# /OPT:REF: code and data nothing references are left out (8.2 MB -> 5.1 MB, mostly the per-file copies of
# gameapi.h's initialisers and unused library code); the static objects' initialisers stay, as .CRT$XCU references
# them. /OPT:ICF folds identical functions to one address (5.07 -> 4.73 MB): no code compares function pointers (the
# audit of H33c: only null checks) or patches one of our functions by address; a crash or profile address names
# only one of the functions folded there (the map lists them at one address). The debug build keeps the linker's
# default (no /OPT:REF with /INCREMENTAL).
# /MAP: ei-rebuild.map lets the harness crash log (ei-rebuild+offset) be resolved to functions.
target_link_options(ei-rebuild PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>" /MAP)

# The launcher: starts the image (ei_launch). On x64 in the verify build only, for the x64 image's runs; the x64 release
# image starts without it, from the game folder.
if(NOT EI_X64 OR EI_HARNESS)
    add_executable(ei_launch tools/launcher/launch.cpp)
    target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)
endif()

# Replays the recorded function vectors (tests/vectors/vectors.h) against our code without the game:
# the families' sources, their checks, and a stub of ei::Standalone. build.cmd runs it. 32-bit only (its recordings are).
if(NOT EI_X64)
file(GLOB EI_REPLAY_SOURCES CONFIGURE_DEPENDS tests/vectors/replay_*.cpp)
set(EI_VECTORS_GAME_SOURCES
    src/game/ai_block_search.cpp
    src/game/ai_map.cpp
    src/game/ai_local_search.cpp
    src/game/ai_motion.cpp
    src/game/ai_movement.cpp
    src/game/ai_path.cpp
    src/game/ai_path_smooth.cpp
    src/game/ai_region_reach.cpp
    src/game/core_maths.cpp
    src/game/core_refcount.cpp
    src/game/game_buffer.cpp
    src/game/projection.cpp
    src/game/net_coder.cpp
    src/game/random.cpp
    src/game/script_blocks.cpp
    src/game/script_group.cpp
    src/game/script_load.cpp
    src/game/script_parse.cpp
    src/game/script_parse_load.cpp
    src/game/unit_db_globals.cpp
    src/game/options_construct.cpp
    src/game/script_vars.cpp
    src/game/sector_spiral.cpp
    src/game/settings_document.cpp
    src/game/unit_stats.cpp
    src/game/unit_params.cpp
    src/game/unit_skill_price.cpp
    src/lib/crt.cpp
    src/lib/crt_format.cpp
    src/lib/crt_maths.cpp
    src/lib/hash_map.cpp
    src/lib/pc24.cpp
    src/lib/rebirth.cpp
    src/lib/str.cpp
    src/lib/text_case.cpp)
list(TRANSFORM EI_VECTORS_GAME_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")
# EI_VECTORS_REPLAY: the game's data at fixed addresses comes from the checks' stand-in (src/game/game_data.h).
add_executable(ei_vectors ${EI_REPLAY_SOURCES} tests/vectors/hook_stub.cpp ${EI_VECTORS_GAME_SOURCES})
target_include_directories(ei_vectors PRIVATE include src)
target_compile_definitions(ei_vectors PRIVATE EI_VECTORS_DATA="${CMAKE_SOURCE_DIR}/tests/vectors/data" EI_VECTORS_REPLAY)
target_link_libraries(ei_vectors PRIVATE ei_platform)
endif()

# Offline check of the interface text layout (render_text): DrawText rules on synthetic cases, and a
# report against GDI on the game's strings (tools/text_check; GDI only there).
add_executable(ei_text_check tools/text_check/text_check.cpp tools/text_check/titles_check.cpp)
target_include_directories(ei_text_check PRIVATE src)
target_link_libraries(ei_text_check PRIVATE ei_render gdi32 user32 shell32)
# Offline check of the interface canvases (render_ui, ticket N1) on a WARP device: every command against
# a CPU reference, and fills, frames, lines and pixels against GDI drawing into a DIB section (GDI only there).
add_executable(ei_ui_check tools/ui_check/ui_check.cpp)
target_include_directories(ei_ui_check PRIVATE include src third_party/sokol)
target_link_libraries(ei_ui_check PRIVATE ei_render gdi32 user32)
# Offline check of the performance overlay (render_perf, ticket R21) on a WARP device: its text, its pixels over a
# back buffer, the GPU timestamps.
add_executable(ei_perf_check tools/perf_check/perf_check.cpp)
target_include_directories(ei_perf_check PRIVATE include src third_party/sokol)
target_link_libraries(ei_perf_check PRIVATE ei_render)

