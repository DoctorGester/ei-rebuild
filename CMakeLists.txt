cmake_minimum_required(VERSION 3.24)
# Two systems: Windows with MSVC (build.cmd: the release and verify builds, 32-bit x86 or x64) and Linux with gcc or
# clang (docs/notes/linux.md: the game on SDL3, POSIX and OpenGL). The harness (for now) and the offline checks against
# Windows' drawing and keys build on Windows.
project(EvilIslands CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# MSVC's runtime linked statically (only MSVC reads it), set before SDL so that its library takes the same.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Two builds (build.cmd): `release` is the product (ei-rebuild.exe, the whole game); `verify` is the harness build,
# the same program with the harness linked in (tests/verify: scenario runs of the image). On x64 (build.cmd
# verify-x64) that build's ei_launch starts the x64 image (run_all.py --x64).
option(EI_HARNESS "Build the harness, linked into ei-rebuild.exe" OFF)

# SDL3 (third_party/SDL; docs/notes/platform.md, "SDL3"): the window, its events, the cursor, the mouse, the clipboard,
# the message box, the clocks and the threads of the builds without the harness (src/platform/sdl), the sound device
# of every build (src/audio/audio.cpp) and on Linux the OpenGL context. Linked statically, so the exe stays one
# file dropped into a game folder (S23), with only the subsystems those use. Added before the game's compile options:
# its warnings are its own.
foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_GPU SDL_RENDER SDL_CAMERA
            SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG SDL_OPENGLES SDL_VULKAN SDL_OFFSCREEN
            SDL_DISKAUDIO SDL_DUMMYAUDIO SDL_DUMMYVIDEO)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL SYSTEM)

# Every string inside the game is UTF-8 (docs/notes/standalone.md section 4, src/game/asset_text.h): asset, save and
# network text is converted from and to EI_ASSET_CODEPAGE where it crosses.
set(EI_ASSET_CODEPAGE 1251 CACHE STRING "The code page of the assets', the saves' and the network's text")
add_compile_definitions(EI_ASSET_CODEPAGE=${EI_ASSET_CODEPAGE})
# The NaN audit's log (src/lib/nan_audit.h) in the verify and debug builds.
add_compile_definitions($<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)

# What differs between the systems: the compiler's options and the platform's and the renderer's backends. Floating
# point is SSE2 on both, in the order the code writes it (MSVC's default /fp:precise: /fp:fast regrouped sums and
# drifted by an ulp, SV7d1; on gcc and clang no contraction of a multiply and an add into one FMA).
if(WIN32)
    enable_language(RC)
    set(EI_RESOURCES src/main.rc)
    add_compile_options(/W4 /utf-8)
    # Inline expansion (R16c5): RelWithDebInfo's /Ob1 inlines only functions marked inline; /Ob2 also the compiler's
    # own picks (file-local helpers).
    string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    # No incremental linking: an incremental relink left out new code from ei_render.lib while reporting success
    # (R16c2), so a run could test stale code.
    string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
           "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
    # /OPT:REF leaves out code and data nothing references; /OPT:ICF folds identical functions to one address (no code
    # compares function pointers: H33c), so a crash or profile address names one of the functions folded there; the
    # debug build keeps the linker's default. /MAP: ei-rebuild.map resolves the harness's crash log
    # (ei-rebuild+offset) to functions.
    add_link_options("$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>" /MAP)
    # AddressSanitizer (build.cmd asan: the verify build with it, into build\asan; docs/notes/harness.md): every target
    # instrumented (the STL's container annotations must match across the libraries), the runtime DLL copied beside
    # the executables and 4 GB of address space for its shadow, redzones and quarantine. Its reports go to the run's
    # folder (tests/verify/image/asan_report.cpp). Without it the executables load their DLLs from System32 only: a
    # game folder may hold a wrapper of the same name (dgVoodoo's beside the original's game.exe).
    option(EI_ASAN "Build with AddressSanitizer (/fsanitize=address)" OFF)
    if(EI_ASAN)
        add_compile_options(/fsanitize=address)
        add_link_options(/LARGEADDRESSAWARE)
        get_filename_component(EI_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(COPY "${EI_CL_DIR}/clang_rt.asan_dynamic-i386.dll" DESTINATION "${CMAKE_BINARY_DIR}")
    else()
        add_link_options(/DEPENDENTLOADFLAG:0x800)
    endif()
    set(EI_SYSTEM win32)   # src/platform/win32, with game.exe's import slots (gameapi.h)
    set(EI_GRAPHICS d3d11)
    set(EI_GRAPHICS_BACKEND SOKOL_D3D11)
    set(EI_GRAPHICS_LIBRARIES d3d11 dxgi)
    set(EI_SHDC_OPTIONS --bytecode --errfmt msvc)   # the HLSL compiled here, so no shader compiler runs at start
else()
    add_compile_options(-ffp-contract=off)
    find_package(OpenGL REQUIRED)
    set(EI_SYSTEM posix)   # src/platform/posix
    set(EI_GRAPHICS gl)
    set(EI_GRAPHICS_BACKEND SOKOL_GLCORE)
    set(EI_GRAPHICS_LIBRARIES SDL3::SDL3-static OpenGL::GL)
endif()

# The game code, with its sound (src/audio: our mixer on SDL3's audio device, the dr_wav / dr_mp3 decoders;
# docs/notes/audio.md) and its movies (src/video: our Bink 1 decoder): a library linked whole (every object, as if
# listed: the static objects' initialisers too), ahead of the renderer and the platform it calls.
file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp src/audio/*.cpp src/video/*.cpp)
add_library(ei_game STATIC ${EI_GAME_SOURCES})
target_include_directories(ei_game PRIVATE include src third_party)
target_link_libraries(ei_game PUBLIC ei_render ei_platform PRIVATE SDL3::SDL3-static)

# The new renderer (docs/notes/renderer.md): sokol_gfx with its graphics backend (src/platform/graphics.h; Direct3D 11
# on Windows, src/platform/d3d11, OpenGL 4.5 on Linux, src/platform/gl; each holds sokol's implementation). The backend
# and the floating-point guard are built in here, not into ei_platform: the offline checks link the renderer without
# the game.
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp src/platform/${EI_GRAPHICS}/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES} src/platform/floating_point.cpp)
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol)
target_compile_definitions(ei_render PRIVATE ${EI_GRAPHICS_BACKEND})
target_link_libraries(ei_render PRIVATE ${EI_GRAPHICS_LIBRARIES})
# Its shaders (docs/notes/renderer.md, "Shader authoring"): src/render/shaders/*.glsl, one input (shaders.glsl)
# compiled by sokol-shdc for every graphics backend into shaders/shaders.h in the build folder (not committed): the
# uniform blocks' C structs, the bind slots and each program's sokol shader description. sokol-shdc: -DEI_SHDC
# (build.cmd passes this machine's), else build/tools in the checkout, else PATH.
find_program(EI_SHDC sokol-shdc HINTS ${CMAKE_SOURCE_DIR}/build/tools REQUIRED)
file(GLOB EI_SHADER_SOURCES CONFIGURE_DEPENDS src/render/shaders/*.glsl)
set(EI_SHADER_HEADER ${CMAKE_BINARY_DIR}/shaders/shaders.h)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
add_custom_command(OUTPUT ${EI_SHADER_HEADER}
    COMMAND ${EI_SHDC} --input ${CMAKE_SOURCE_DIR}/src/render/shaders/shaders.glsl --output ${EI_SHADER_HEADER}
            --slang hlsl5:metal_macos:glsl430 --format sokol_impl --ifdef --no-log-cmdline ${EI_SHDC_OPTIONS}
    DEPENDS ${EI_SHADER_SOURCES} ${EI_SHDC}
    COMMENT "Compiling the shaders with sokol-shdc"
    VERBATIM)
target_sources(ei_render PRIVATE ${EI_SHADER_HEADER})
target_include_directories(ei_render PUBLIC ${CMAKE_BINARY_DIR})
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

# The platform layer (docs/notes/platform.md): the game's calls of the operating system (src/platform/platform.h). The
# window, its events, the cursor, the mouse, the clipboard, the message box, the clocks and the threads on SDL3
# (src/platform/sdl); the rest on the system's (src/platform/win32, the one place of the game's code that includes
# <windows.h>, or src/platform/posix).
file(GLOB EI_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/sdl/*.cpp src/platform/${EI_SYSTEM}/*.cpp)
add_library(ei_platform STATIC ${EI_PLATFORM_SOURCES})
target_include_directories(ei_platform PRIVATE include src)
target_link_libraries(ei_platform PRIVATE SDL3::SDL3-static)

# The game (docs/notes/standalone.md section 2): ei-rebuild.exe, an ordinary exe (S18k: ASLR as any exe, no read of
# game.exe) with the whole game linked in (S23: no DLL; dropped into a game folder it plays from there). Its entry is
# main (src/main.cpp, both systems); on Windows its one resource is its version information (src/main.rc, C23):
# game.exe's own resources are not used.
set(EI_PROGRAM_SOURCES src/hook/log.cpp src/hook/rebirth_hook.cpp)
add_executable(ei-rebuild WIN32 src/main.cpp ${EI_PROGRAM_SOURCES} ${EI_RESOURCES})
target_include_directories(ei-rebuild PRIVATE include src)
target_link_libraries(ei-rebuild PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
# The Rebirth install's profile, which an exe takes in a Rebirth folder (src/hook/rebirth_hook.cpp):
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
set_source_files_properties(src/hook/rebirth_hook.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/config)
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
     "Third-party software built into ei-rebuild, with its licences.\n\n${EI_NOTICES}")

if(EI_HARNESS)
    # The harness (tests/verify; docs/notes/harness.md), linked into this exe. Its own window, events, clocks, cursor,
    # mouse and clipboard on SDL (tests/verify/harness_platform.cpp) take platform_sdl.cpp's place; sokol's validation
    # layer is on (its messages in ei-rebuild.log), with the Direct3D backend's removable device
    # (src/platform/d3d11/graphics_d3d11.h), which the harness's image side (tests/verify/image) reaches.
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp tests/verify/image/*.cpp)
    target_sources(ei-rebuild PRIVATE ${EI_HARNESS_SOURCES})
    target_include_directories(ei-rebuild PRIVATE third_party/sokol)
    target_link_libraries(ei-rebuild PRIVATE SDL3::SDL3-static)
    set_source_files_properties(src/platform/sdl/platform_sdl.cpp PROPERTIES HEADER_FILE_ONLY ON)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
endif()

if(WIN32)
    # The game is a windowed program whose entry is main.
    target_link_options(ei-rebuild PRIVATE /ENTRY:mainCRTStartup)

    # The launcher: starts the image (ei_launch), the harness's runs and play.cmd's.
    add_executable(ei_launch tools/launcher/launch.cpp)
    target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)
    target_include_directories(ei_launch PRIVATE src)

    # Replays the recorded function vectors (tests/vectors/vectors.h) against our code without the game: the
    # families' sources, their checks, and a stub of ei::Standalone. build.cmd runs it. 32-bit only (its recordings
    # are).
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
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
    # Offline check of the SDL backend's keys (src/platform/sdl/sdl_keys.cpp, ticket PL9a) against the key codes and control
    # characters Windows' layouts give, for every layout the machine has.
    add_executable(ei_input_check tools/input_check/input_check.cpp src/platform/sdl/sdl_keys.cpp)
    target_include_directories(ei_input_check PRIVATE include src)
    target_link_libraries(ei_input_check PRIVATE SDL3::SDL3-static user32)
    # Offline check of the interface canvases (render_ui, ticket N1) on a WARP device: every command against
    # a CPU reference, and fills, frames, lines and pixels against GDI drawing into a DIB section (GDI only there).
    add_executable(ei_ui_check tools/ui_check/ui_check.cpp tests/render/glyph_cases.cpp)
    target_include_directories(ei_ui_check PRIVATE include src tests third_party/sokol)
    target_link_libraries(ei_ui_check PRIVATE ei_render gdi32 user32)
    # Offline check of the performance overlay (render_perf, ticket R21) on a WARP device: its text, its pixels over a
    # back buffer, the GPU timestamps.
    add_executable(ei_perf_check tools/perf_check/perf_check.cpp)
    target_include_directories(ei_perf_check PRIVATE include src third_party/sokol)
    target_link_libraries(ei_perf_check PRIVATE ei_render)
else()
    # Linux's checks (ctest; without a display: xvfb-run ctest): the POSIX platform's (paths, files, .ini files, the
    # one-instance lock, sockets, the settings document; tests/port/port_check.cpp) and the interface canvases' glyph
    # cases on OpenGL (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 11).
    enable_testing()
    add_executable(ei_port_check tests/port/port_check.cpp src/lib/text_case.cpp src/game/settings_document.cpp
                   tests/vectors/replay_settings.cpp)
    target_include_directories(ei_port_check PRIVATE include src)
    target_link_libraries(ei_port_check PRIVATE ei_platform)
    add_test(NAME port_platform COMMAND ei_port_check)
    add_executable(ei_render_check tests/port/render_check.cpp tests/render/glyph_cases.cpp ${EI_PROGRAM_SOURCES})
    target_include_directories(ei_render_check PRIVATE include src tests third_party/sokol)
    target_link_libraries(ei_render_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
    add_test(NAME port_render COMMAND ei_render_check)
    set_tests_properties(port_platform port_render PROPERTIES TIMEOUT 60)
endif()
