cmake_minimum_required(VERSION 3.24)
# MSVC's debug information inside each object (/Z7, CMAKE_MSVC_DEBUG_INFORMATION_FORMAT below) instead of one PDB per
# target that every compile writes into (/Zi): the compiler cache (build.cmd, tools/compiler_cache.py) keeps such objects.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
# Three systems: Windows with MSVC (build.cmd: the release and verify builds, x64), Linux with gcc or clang
# (docs/notes/linux.md: the game on SDL3, POSIX and Vulkan) and macOS with Apple Clang (docs/notes/macos.md: SDL3,
# POSIX and Metal). The offline checks against Windows' drawing and keys build on Windows.
project(EvilIslands CXX C)
# The step wrapper (build.cmd: tools/compiler_cache.py's EI_STEP_WRAPPER, as CMAKE_CXX_LINKER_LAUNCHER) also in front of
# the archiver: a link or archive step that fails with no output then prints its exit code (H52a).
if(WIN32 AND CMAKE_CXX_LINKER_LAUNCHER)
    list(JOIN CMAKE_CXX_LINKER_LAUNCHER " " EI_STEP_WRAPPER_TEXT)
    set(CMAKE_CXX_CREATE_STATIC_LIBRARY
        "${EI_STEP_WRAPPER_TEXT} <CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS> ")
endif()
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# MSVC's runtime linked statically (only MSVC reads it), set before SDL so that its library takes the same.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Two builds (build.cmd): `release` is the product (ei-rebuild.exe, the whole game); `verify` is the harness build,
# the same program with the harness linked in (tests/verify: scenario runs of the image).
option(EI_HARNESS "Build the harness, linked into ei-rebuild.exe" OFF)

# SDL3 (third_party/SDL; docs/notes/platform.md, "SDL3"): the window, its events, the cursor, the mouse, the clipboard,
# the message box, the clocks and the threads of the builds without the harness (src/platform/sdl), the sound device
# of every build (src/audio/audio.cpp) and the renderer's GPU (SDL_GPU: Direct3D 12, Vulkan; src/render/gpu.cpp).
# Linked statically, so the exe stays one file dropped into a game folder (S23), with only the subsystems those use.
# Added before the game's compile options: its warnings are its own.
foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_RENDER SDL_CAMERA
            SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG SDL_OPENGL SDL_OPENGLES SDL_OFFSCREEN
            SDL_DISKAUDIO SDL_DUMMYAUDIO SDL_DUMMYVIDEO)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
# macOS's folder picker (src/platform/posix/paths.cpp: the game's folder when none is beside the app).
if(APPLE)
    set(SDL_DIALOG ON CACHE BOOL "" FORCE)
endif()
# SDL's GPU API (Direct3D 12, Vulkan): the renderer's (docs/notes/renderer.md, "gpu.h").
set(SDL_GPU ON CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
# SDL's configure checks whose results MSVC for x64 with the Windows 10 SDK always gives, taken as known (a check skips a
# result already in the cache), as SDL's own SDL_PRESEED does for others: they took 6 s of each fresh configure.
if(MSVC)
    foreach(check SSE SSE2 SSE3 SSE4_1 SSE4_2 AVX AVX2 AVX512F)
        set(COMPILER_SUPPORTS_${check} 1 CACHE INTERNAL "")
    endforeach()
    set(COMPILER_SUPPORTS_MMX "" CACHE INTERNAL "")
    foreach(check LIBC_HAS__COPYSIGN LIBC_HAS__FSEEKI64 HAVE_DXGI1_5_H HAVE_DXGI1_6_H HAVE_MFAPI_H)
        set(${check} 1 CACHE INTERNAL "")
    endforeach()
endif()
# No precompiled header (SDL's own): the compiler cache does not keep objects compiled with one.
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL SYSTEM)
# miniz (third_party/miniz, unmodified; C): the zip reader of the zip mods (src/platform/zip_folders.cpp; docs/notes/
# mods.md, "Zip mods") and deflate. Added before the game's compile options, with none of its warnings shown: they are
# its own. No stdio (files go through the platform) and no clock.
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz SYSTEM INTERFACE third_party)
target_compile_definitions(miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME)
target_compile_options(miniz PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(miniz PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
# stb_dxt (third_party/stb, unmodified; built by our stb_dxt_impl.c): the PNG textures' DXT blocks (src/engine/models/
# texture_png.cpp; docs/notes/mods.md, "Textures as PNG"). Without its warnings, as miniz.
add_library(stb_dxt STATIC third_party/stb/stb_dxt_impl.c)
target_include_directories(stb_dxt PRIVATE third_party)
target_include_directories(stb_dxt SYSTEM INTERFACE third_party)
target_compile_options(stb_dxt PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(stb_dxt PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
# Wuffs (third_party/wuffs, unmodified; built by our wuffs_png.c, its PNG modules only): the textures' PNG reader
# (src/lib/png.h; docs/notes/mods.md, "Textures as PNG"). Without its warnings, as miniz.
add_library(wuffs STATIC third_party/wuffs/wuffs_png.c)
target_include_directories(wuffs PRIVATE third_party)
target_include_directories(wuffs SYSTEM INTERFACE third_party)
target_compile_options(wuffs PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(wuffs PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
# cgltf (third_party/cgltf, unmodified; built by our cgltf_impl.c): the figure models read as glTF
# (src/engine/models/figure_gltf.cpp; docs/notes/mods.md, "Models as glTF"). Without its warnings, as miniz.
add_library(cgltf STATIC third_party/cgltf/cgltf_impl.c)
target_include_directories(cgltf PRIVATE third_party)
target_include_directories(cgltf SYSTEM INTERFACE third_party)
target_compile_options(cgltf PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(cgltf PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
# tomlc17 (third_party/tomlc17, C17): the TOML reader of the record files (src/lib/toml.h; docs/notes/mods.md, "Records
# as TOML"). Added before the game's compile options and without warnings: its warnings are its own.
enable_language(C)
add_library(tomlc17 STATIC third_party/tomlc17/tomlc17.c)
set_target_properties(tomlc17 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)
target_compile_options(tomlc17 PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
# Lua 5.3 with Eris (third_party/eris, unmodified): the mission scripts' Lua runtime and its persistence
# (src/game/script/script_lua.cpp; docs/notes/lua-scripting.md). Compiled as C++: a Lua error then unwinds through the
# bindings' frames as an exception (Lua's own choice for C++), not as a longjmp past their destructors. POSIX's
# functions outside Windows (mkstemp, not tmpnam). Without its warnings, as tomlc17.
file(GLOB EI_ERIS_SOURCES CONFIGURE_DEPENDS third_party/eris/*.c)
add_library(eris STATIC ${EI_ERIS_SOURCES})
set_source_files_properties(${EI_ERIS_SOURCES} PROPERTIES LANGUAGE CXX)
target_include_directories(eris SYSTEM INTERFACE third_party)
if(NOT WIN32)
    target_compile_definitions(eris PUBLIC LUA_USE_POSIX)
endif()
target_compile_options(eris PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(eris PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Dear ImGui (third_party/imgui, unmodified, without its backends): the panels of the tools built into ei-rebuild
# (src/tools: the effect viewer, ei-rebuild --effects), drawn by the renderer's overlay (src/render/render_overlay.cpp).
# Without its warnings, as eris.
add_library(imgui STATIC third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp
            third_party/imgui/imgui_widgets.cpp)
target_include_directories(imgui SYSTEM INTERFACE third_party)
target_compile_options(imgui PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)
set_target_properties(imgui PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Every string inside the game is UTF-8 (docs/notes/standalone.md section 4, src/engine/core/asset_text.h): asset, save and
# network text is converted from and to text::kAssetCodePage where it crosses.

# What differs between the systems: the compiler's options and the platform's and the renderer's backends. Floating
# point stays in the order the code writes it (MSVC's default /fp:precise: /fp:fast regrouped sums and
# drifted by an ulp, SV7d1; on gcc and clang no contraction of a multiply and an add into one FMA).
if(WIN32)
    enable_language(RC)
    set(EI_RESOURCES src/main.rc)
    # Not for MASM (the coverage build's harness_coverage_entry.asm), which knows neither.
    add_compile_options("$<$<NOT:$<COMPILE_LANGUAGE:ASM_MASM>>:/W4;/utf-8>")
    # Inline expansion (R16c5): RelWithDebInfo's /Ob1 inlines only functions marked inline; /Ob2 also the compiler's
    # own picks (file-local helpers).
    string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    # No incremental linking: an incremental relink left out new code from ei_render.lib while reporting success
    # (R16c2), so a run could test stale code; the debug build's failed once with LNK1000 "Internal error during
    # IncrBuildImage" (H52a).
    foreach(config DEBUG RELWITHDEBINFO)
        string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_${config}
               "${CMAKE_EXE_LINKER_FLAGS_${config}}")
    endforeach()
    # /OPT:REF leaves out code and data nothing references; /OPT:ICF folds identical functions to one address (no code
    # compares function pointers: H33c), so a crash or profile address names one of the functions folded there; the
    # debug build keeps the linker's default. /MAP: ei-rebuild.map resolves the harness's crash log
    # (ei-rebuild+offset) to functions.
    # Source coverage (build.cmd coverage: the verify build with it, into build\coverage; docs/notes/harness.md,
    # "Source coverage"; tools/coverage.py): src/ compiled with MSVC's SanitizerCoverage (a flag byte per edge) and /Gh
    # (each function's entry calls tests/verify/harness_coverage_entry.asm's _penter), at /Ob1 so that file-local
    # helpers stay functions of their own. It keeps every function (/OPT:NOREF), so the ones nothing calls count as
    # unreached; only ei-rebuild is built.
    option(EI_COVERAGE "Build src/ with source coverage (MSVC /fsanitize-coverage and /Gh)" OFF)
    if(EI_COVERAGE)
        string(REPLACE "/Ob2" "/Ob1" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        add_link_options(/OPT:NOREF /OPT:NOICF /MAP)
        enable_language(ASM_MASM)
    else()
        add_link_options("$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>" /MAP)
    endif()
    # AddressSanitizer (build.cmd asan: the verify build with it, into build\asan; docs/notes/harness.md): every target
    # instrumented (the STL's container annotations must match across the libraries) and the runtime DLL copied beside
    # the executables. Its reports go to the run's
    # folder (tests/verify/image/asan_report.cpp). Without it the executables load their DLLs from System32 only: a
    # game folder may hold a wrapper of the same name (dgVoodoo's beside the original's game.exe).
    option(EI_ASAN "Build with AddressSanitizer (/fsanitize=address)" OFF)
    if(EI_ASAN)
        add_compile_options(/fsanitize=address)
        get_filename_component(EI_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(COPY "${EI_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll" DESTINATION "${CMAKE_BINARY_DIR}")
    else()
        add_link_options(/DEPENDENTLOADFLAG:0x800)
    endif()
    set(EI_SYSTEM win32)   # src/platform/win32
else()
    add_compile_options(-ffp-contract=off)
    set(EI_SYSTEM posix)   # src/platform/posix (Linux and macOS)
endif()

# The engine (docs/notes/file-layout.md, "The engine library"): src/lib and src/engine's folders: the data and its
# archives (data), the models (textures, figures, animation), the scene (view, lights, clip, pipeline, scene renderer),
# the core objects (engine objects, transforms, references, streams, random), the mods' file layers (mods) and the
# particle core (particles), for the game and later the effect viewer's own exe (MF15k4). It includes no game header
# (tools/engine_layers.py, a build check). An object library as the game's (below), every object linked in; an object
# library's objects reach only the targets that link it themselves, so each program that links ei_game links it too.
file(GLOB EI_ENGINE_SOURCES CONFIGURE_DEPENDS src/lib/*.cpp src/engine/*/*.cpp)
add_library(ei_engine OBJECT ${EI_ENGINE_SOURCES})
target_include_directories(ei_engine PRIVATE src third_party)
target_link_libraries(ei_engine PUBLIC ei_render ei_platform tomlc17 stb_dxt wuffs cgltf PRIVATE SDL3::SDL3-static)
# The original effects on data (assets/effects/*.effect.toml) built into the engine as bytes at build time
# (src/engine/particles/embed_effects.cmake; docs/notes/effects.md, "The original effects on data").
file(GLOB EI_BUILTIN_EFFECTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/effects/*.effect.toml)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/effect_files/builtin.h
    COMMAND ${CMAKE_COMMAND} -DFOLDER=${CMAKE_SOURCE_DIR}/assets/effects -DLABEL=assets/effects
            -DOUT=${CMAKE_BINARY_DIR}/effect_files/builtin.h -DNAMESPACE=builtin_effects -DSUFFIX=.effect.toml
            -P ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    DEPENDS ${EI_BUILTIN_EFFECTS} ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    COMMENT "Writing assets/effects as bytes"
    VERBATIM)
target_sources(ei_engine PRIVATE ${CMAKE_BINARY_DIR}/effect_files/builtin.h)

# The game code on the engine, with its sound (src/audio: our mixer on SDL3's audio device, the dr_wav / dr_mp3
# decoders; docs/notes/sound.md) and its movies (src/video: our Bink 1 decoder): an object library, every object linked
# in (the static objects' initialisers too) ahead of the renderer and the platform it calls. No archive of them: with
# the debug information inside each object it was 1.1 GB, rewritten on every change. src/game's files are all in its
# folders (src/game/units/unit.cpp; docs/notes/file-layout.md: layout.py check refuses a flat one).
file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*/*.cpp src/audio/*.cpp src/video/*.cpp)
add_library(ei_game OBJECT ${EI_GAME_SOURCES})
target_include_directories(ei_game PRIVATE src third_party)
target_link_libraries(ei_game PUBLIC ei_engine ei_render ei_platform tomlc17 stb_dxt wuffs cgltf eris
                      PRIVATE SDL3::SDL3-static)
# The game's spells on scripts (assets/spells: their Lua files and the records' fields, spells.sdb.toml), the same
# way (docs/notes/items_spells.md, "Scripted spells").
file(GLOB EI_BUILTIN_SPELLS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/spells/*)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/spell_files/builtin.h
    COMMAND ${CMAKE_COMMAND} -DFOLDER=${CMAKE_SOURCE_DIR}/assets/spells -DLABEL=assets/spells
            -DOUT=${CMAKE_BINARY_DIR}/spell_files/builtin.h -DNAMESPACE=builtin_spells
            -P ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    DEPENDS ${EI_BUILTIN_SPELLS} ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    COMMENT "Writing assets/spells as bytes"
    VERBATIM)
target_sources(ei_game PRIVATE ${CMAKE_BINARY_DIR}/spell_files/builtin.h)
# The game's own units.udb fields (assets/units/units.udb.toml: the models' blood and shock looks), the same way
# (docs/modding.md, "Databases").
file(GLOB EI_BUILTIN_UNITS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/units/*)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/unit_files/builtin.h
    COMMAND ${CMAKE_COMMAND} -DFOLDER=${CMAKE_SOURCE_DIR}/assets/units -DLABEL=assets/units
            -DOUT=${CMAKE_BINARY_DIR}/unit_files/builtin.h -DNAMESPACE=builtin_units
            -P ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    DEPENDS ${EI_BUILTIN_UNITS} ${CMAKE_SOURCE_DIR}/src/engine/particles/embed_effects.cmake
    COMMENT "Writing assets/units as bytes"
    VERBATIM)
target_sources(ei_game PRIVATE ${CMAKE_BINARY_DIR}/unit_files/builtin.h)

# The new renderer (docs/notes/renderer.md) on SDL3's GPU API (src/render/gpu.h: Direct3D 12 on Windows, Vulkan
# elsewhere). Not part of ei_platform: the offline checks link the renderer without the game.
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES})
target_include_directories(ei_render PRIVATE src third_party)
target_link_libraries(ei_render PRIVATE SDL3::SDL3-static)
# Its programs (docs/notes/renderer.md, "HLSL and SDL_shadercross"): each src/render/shaders/<program>.hlsl is one
# program (vs_main and ps_main, each stage under EI_VERTEX / EI_FRAGMENT), compiled by SDL_shadercross
# (tools/shader_toolchain) to SPIR-V, MSL and on Windows DXIL; spirv-cross reflects each stage's SPIR-V, and
# tools/gen_gpu_shaders.py checks the binding rules and writes shaders/gpu_shaders.h (the blocks' structs, the slots,
# the counts) and shaders/gpu_shaders.cpp (the code) into the build folder (not committed); the target ei_shaders
# builds just those two. EI_PREBUILT_SHADERS names a folder with another build's two files (its shaders/, built for the
# same system) to copy instead: no toolchain, no DXC, no Python. The Release workflow's Linux job uses it, its
# Ubuntu 22.04 container being too old for DXC's release (docs/notes/linux.md, "Release").
set(EI_PREBUILT_SHADERS "" CACHE PATH "A folder with gpu_shaders.h and .cpp to use instead of compiling the HLSL")
set(EI_GPU_SHADERS ${CMAKE_BINARY_DIR}/shaders/gpu_shaders.h ${CMAKE_BINARY_DIR}/shaders/gpu_shaders.cpp)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
if(EI_PREBUILT_SHADERS)
    foreach(file gpu_shaders.h gpu_shaders.cpp)
        if(NOT EXISTS ${EI_PREBUILT_SHADERS}/${file})
            message(FATAL_ERROR "EI_PREBUILT_SHADERS '${EI_PREBUILT_SHADERS}' has no ${file}")
        endif()
        configure_file(${EI_PREBUILT_SHADERS}/${file} ${CMAKE_BINARY_DIR}/shaders/${file} COPYONLY)
    endforeach()
else()
    include(tools/shader_toolchain/toolchain.cmake)
    # On Windows the interpreter build.cmd runs (python on the PATH): FindPython's search of every PATH folder for every
    # versioned name took 2 s of each fresh configure.
    if(WIN32)
        find_program(Python3_EXECUTABLE python)
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(GLOB EI_HLSL_PROGRAMS CONFIGURE_DEPENDS src/render/shaders/*.hlsl)
    file(GLOB EI_HLSL_SOURCES CONFIGURE_DEPENDS src/render/shaders/*.hlsl src/render/shaders/*.hlsli)
    set(EI_HLSL_DIR ${CMAKE_BINARY_DIR}/shaders/hlsl)
    file(MAKE_DIRECTORY ${EI_HLSL_DIR})
    set(EI_HLSL_FORMATS spirv msl)
    if(WIN32)
        list(APPEND EI_HLSL_FORMATS dxil)
    endif()
    set(EI_HLSL_OUTPUTS)
    set(EI_HLSL_NAMES)
    foreach(source ${EI_HLSL_PROGRAMS})
        get_filename_component(program ${source} NAME_WE)
        list(APPEND EI_HLSL_NAMES ${program})
        foreach(stage vertex fragment)
            if(stage STREQUAL "vertex")
                set(entry vs_main)
                set(define -DEI_VERTEX)
            else()
                set(entry ps_main)
                set(define -DEI_FRAGMENT)
            endif()
            foreach(format ${EI_HLSL_FORMATS})
                set(out ${EI_HLSL_DIR}/${program}_${stage}.${format})
                string(TOUPPER ${format} dest)
                if(format STREQUAL "msl")
                    # MSL from the SPIR-V by spirv-cross, each texture, sampler and block at its SPIR-V binding:
                    # SDL_GPU's Metal slots. shadercross's own MSL left out a texture whose sampler the stage never
                    # reads (shadow_receiver.hlsli's shadow_range, read with Load) and put it at texture(0), over tex0.
                    add_custom_command(OUTPUT ${out}
                        COMMAND ${EI_SPIRV_CROSS} ${EI_HLSL_DIR}/${program}_${stage}.spirv --msl --msl-version 10200
                                --msl-decoration-binding --output ${out}
                        DEPENDS ${EI_HLSL_DIR}/${program}_${stage}.spirv
                        COMMENT "spirv-cross ${program} ${stage} to msl"
                        VERBATIM)
                else()
                    add_custom_command(OUTPUT ${out}
                        COMMAND ${EI_SHADERCROSS} ${source} -s HLSL -d ${dest} -t ${stage} -e ${entry} ${define}
                                -I ${CMAKE_SOURCE_DIR}/src/render/shaders -o ${out}
                        DEPENDS ${EI_HLSL_SOURCES}
                        COMMENT "shadercross ${program}.hlsl ${stage} to ${format}"
                        VERBATIM)
                endif()
                list(APPEND EI_HLSL_OUTPUTS ${out})
            endforeach()
            set(reflection ${EI_HLSL_DIR}/${program}_${stage}.json)
            add_custom_command(OUTPUT ${reflection}
                COMMAND ${EI_SPIRV_CROSS} ${EI_HLSL_DIR}/${program}_${stage}.spirv --reflect --output ${reflection}
                DEPENDS ${EI_HLSL_DIR}/${program}_${stage}.spirv
                VERBATIM)
            list(APPEND EI_HLSL_OUTPUTS ${reflection})
        endforeach()
    endforeach()
    string(REPLACE ";" "," EI_HLSL_FORMAT_LIST "${EI_HLSL_FORMATS}")
    add_custom_command(OUTPUT ${EI_GPU_SHADERS}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_gpu_shaders.py
                --sources ${CMAKE_SOURCE_DIR}/src/render/shaders --compiled ${EI_HLSL_DIR}
                --out ${CMAKE_BINARY_DIR}/shaders --formats ${EI_HLSL_FORMAT_LIST} ${EI_HLSL_NAMES}
        DEPENDS ${EI_HLSL_OUTPUTS} ${CMAKE_SOURCE_DIR}/tools/gen_gpu_shaders.py
        COMMENT "Writing the HLSL programs' code and reflection (gen_gpu_shaders.py)"
        VERBATIM)
    add_custom_target(ei_shaders DEPENDS ${EI_GPU_SHADERS})
endif()
# The programs' code goes into the renderer, with its device (src/render/gpu.cpp, docs/notes/renderer.md, "gpu.h").
target_sources(ei_render PRIVATE ${EI_GPU_SHADERS})
# Their check (tools/shader_check): build_checks.py runs it on Windows, ctest on Linux.
add_executable(ei_shader_check tools/shader_check/shader_check.cpp)
target_include_directories(ei_shader_check PRIVATE src ${CMAKE_BINARY_DIR})
target_link_libraries(ei_shader_check PRIVATE ei_render SDL3::SDL3-static)
target_include_directories(ei_render PUBLIC ${CMAKE_BINARY_DIR})
# gpu.cpp's check (tools/gpu_check) on Direct3D 12 and Vulkan: build_checks.py runs it on Windows, ctest on Linux.
add_executable(ei_gpu_check tools/gpu_check/gpu_check.cpp tools/gpu_check/gpu_bench.cpp)
target_include_directories(ei_gpu_check PRIVATE src ${CMAKE_BINARY_DIR})
target_link_libraries(ei_gpu_check PRIVATE ei_render SDL3::SDL3-static)
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
# (src/platform/sdl); the folder calls on std::filesystem (src/platform/files.cpp); the rest on the system's
# (src/platform/win32, the one place of the game's code that includes <windows.h>, or src/platform/posix; one file per
# area).
file(GLOB EI_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/sdl/*.cpp src/platform/${EI_SYSTEM}/*.cpp)
list(APPEND EI_PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/files.cpp ${CMAKE_SOURCE_DIR}/src/platform/path_layers.cpp
     ${CMAKE_SOURCE_DIR}/src/platform/zip_folders.cpp ${CMAKE_SOURCE_DIR}/src/platform/seams.cpp)
# What a harness run answers itself: the sockets (src/platform/<system>/sockets.cpp), the platform's seams
# (src/platform/seams.cpp: the file calls' paths, the write watch, the isolated run) and SDL's areas
# (src/platform/sdl/platform_sdl.cpp). In the harness's builds the game takes the harness's backend's definitions
# (tests/verify/platform, below), and the other programs that link the platform take the files (EI_GAME_CALLS;
# EI_SDL_CALLS, with SDL3 linked, for those that need SDL's areas).
set(EI_GAME_CALLS)
set(EI_SDL_CALLS)
if(EI_HARNESS)
    set(EI_GAME_CALLS ${CMAKE_SOURCE_DIR}/src/platform/${EI_SYSTEM}/sockets.cpp ${CMAKE_SOURCE_DIR}/src/platform/seams.cpp)
    set(EI_SDL_CALLS ${CMAKE_SOURCE_DIR}/src/platform/sdl/platform_sdl.cpp)
    list(REMOVE_ITEM EI_PLATFORM_SOURCES ${EI_GAME_CALLS} ${EI_SDL_CALLS})
endif()
add_library(ei_platform STATIC ${EI_PLATFORM_SOURCES})
target_include_directories(ei_platform PRIVATE src)
target_link_libraries(ei_platform PRIVATE SDL3::SDL3-static miniz)

# The game (docs/notes/standalone.md section 2): ei-rebuild.exe, an ordinary exe (S18k: ASLR as any exe, no read of
# game.exe) with the whole game linked in (S23: no DLL; dropped into a game folder it plays from there). Its entry is
# main (src/main.cpp, every system); on Windows its resources are its icon and version information (src/main.rc, C23,
# C24): game.exe's own resources are not used. The icon is assets/icon: ei-rebuild.ico (16 to 256 px) for Windows,
# ei-rebuild.png (1254 px) for the macOS bundle's .icns, made at build time with sips and iconutil.
# The log is the program's, not ei_platform's: the offline checks that link the platform (tools/ui_check,
# tools/perf_check) have their own ei::Log.
set(EI_PROGRAM_SOURCES src/platform/log.cpp)
if(APPLE)
    set(icon_set ${CMAKE_BINARY_DIR}/icon/ei-rebuild.iconset)
    set(EI_ICNS ${CMAKE_BINARY_DIR}/icon/ei-rebuild.icns)
    set(icon_png ${CMAKE_SOURCE_DIR}/assets/icon/ei-rebuild.png)
    set(icon_steps COMMAND ${CMAKE_COMMAND} -E rm -rf ${icon_set} COMMAND ${CMAKE_COMMAND} -E make_directory ${icon_set})
    foreach(size 16 32 128 256 512)
        math(EXPR double "${size} * 2")
        list(APPEND icon_steps
            COMMAND sips -z ${size} ${size} ${icon_png} --out ${icon_set}/icon_${size}x${size}.png
            COMMAND sips -z ${double} ${double} ${icon_png} --out ${icon_set}/icon_${size}x${size}@2x.png)
    endforeach()
    add_custom_command(OUTPUT ${EI_ICNS} ${icon_steps} COMMAND iconutil -c icns ${icon_set} -o ${EI_ICNS}
        DEPENDS ${icon_png} COMMENT "The bundle's icon (sips, iconutil)" VERBATIM)
    set_source_files_properties(${EI_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND EI_RESOURCES ${EI_ICNS})
endif()
add_executable(ei-rebuild WIN32 src/main.cpp ${EI_PROGRAM_SOURCES} ${EI_RESOURCES})
if(WIN32)
    set_property(SOURCE src/main.rc APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/assets/icon/ei-rebuild.ico)
    set_property(SOURCE src/main.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/assets/icon)
endif()
# On macOS an application bundle (ei-rebuild.app/Contents/MacOS/ei-rebuild; docs/notes/macos.md) with its icon.
if(APPLE)
    set_target_properties(ei-rebuild PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER org.eirebuild.game MACOSX_BUNDLE_BUNDLE_NAME "Evil Islands"
        MACOSX_BUNDLE_ICON_FILE ei-rebuild.icns)
endif()
target_include_directories(ei-rebuild PRIVATE src)
target_link_libraries(ei-rebuild PRIVATE ei_game ei_engine)
# The tools built into the game's exe (src/tools; ei-rebuild --effects): only ei-rebuild has them and imgui.
file(GLOB EI_TOOL_SOURCES CONFIGURE_DEPENDS src/tools/*.cpp)
target_sources(ei-rebuild PRIVATE ${EI_TOOL_SOURCES})
target_link_libraries(ei-rebuild PRIVATE imgui)
# ei-convert (tools/convert; docs/notes/mods.md, "Records as TOML" and "Textures as PNG"): an install's record files
# written as TOML and its textures as PNG by the game's own loaders, each one's round trip checked; --scripts, the script
# survey and Lua check (docs/notes/lua-scripting.md, "Survey"). Its own ei::Log (the output's lines).
add_executable(ei-convert tools/convert/convert.cpp tools/convert/convert_databases.cpp tools/convert/convert_maps.cpp
               tools/convert/convert_textures.cpp tools/convert/convert_mobs.cpp
               tools/convert/convert_models.cpp tools/convert/convert_scripts.cpp tools/convert/lua_check.cpp
               tools/convert/lua_api.cpp
               tools/convert/lua_globals.cpp tools/convert/example_mod.cpp tools/convert/effect_check.cpp
               ${CMAKE_BINARY_DIR}/effect_files/reference.h ${CMAKE_BINARY_DIR}/effect_files/coarse.h
               ${EI_GAME_CALLS} ${EI_SDL_CALLS})
target_include_directories(ei-convert PRIVATE src third_party ${CMAKE_BINARY_DIR})
# --effect-check's reference (tests/effects/reference.txt, the original particle types' recorded C++ runs) built into
# the tool as bytes (src/render/embed.cmake).
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/effect_files/reference.h
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR}/tests/effects/reference.txt -DNAME=kReference
            -DOUT=${CMAKE_BINARY_DIR}/effect_files/reference.h -P ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/tests/effects/reference.txt ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
    COMMENT "Writing the effect check's reference as bytes"
    VERBATIM)
# --effect-check --coarse's reference (tests/effects/coarse.txt: how each case looks, over 8 seeds).
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/effect_files/coarse.h
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR}/tests/effects/coarse.txt -DNAME=kCoarse
            -DOUT=${CMAKE_BINARY_DIR}/effect_files/coarse.h -P ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/tests/effects/coarse.txt ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
    COMMENT "Writing the effect check's coarse reference as bytes"
    VERBATIM)
target_link_libraries(ei-convert PRIVATE ei_game ei_engine SDL3::SDL3-static)
# The repository's examples folder copied beside ei-convert, as a release package has it: --example-mod reads its own
# files from there (tools/convert/example_mod.h).
file(GLOB_RECURSE EI_EXAMPLES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*)
set(EI_EXAMPLES_COPIED)
foreach(file ${EI_EXAMPLES})
    file(RELATIVE_PATH relative ${CMAKE_SOURCE_DIR} ${file})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${relative} COMMAND ${CMAKE_COMMAND} -E copy ${file} ${CMAKE_BINARY_DIR}/${relative}
                       DEPENDS ${file} VERBATIM)
    list(APPEND EI_EXAMPLES_COPIED ${CMAKE_BINARY_DIR}/${relative})
endforeach()
add_custom_target(ei_examples ALL DEPENDS ${EI_EXAMPLES_COPIED})
add_dependencies(ei-convert ei_examples)
# The product's notices (third_party_notices.txt beside the executables): the licences of the
# third-party code and fonts built in, the fonts' SIL Open Font License among them.
set(EI_NOTICES)
foreach(part fonts/README.txt fonts/LICENSE SDL/README.txt SDL/LICENSE.txt dr_libs/README.txt dr_libs/LICENSE stb/README.txt
             miniz/README.txt miniz/LICENSE tomlc17/README.txt tomlc17/LICENSE cgltf/README.txt cgltf/LICENSE
             wuffs/README.txt wuffs/LICENSE
             eris/README.txt eris/LICENSE imgui/README.txt imgui/LICENSE.txt)
    file(READ ${CMAKE_SOURCE_DIR}/third_party/${part} text)
    string(APPEND EI_NOTICES "==== third_party/${part}\n\n${text}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/third_party/${part})
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/third_party_notices.txt
     "Third-party software built into ei-rebuild, with its licences.\n\n${EI_NOTICES}")

if(EI_HARNESS)
    # The harness (tests/verify; docs/notes/harness.md), linked into this exe. Its platform backend (tests/verify/platform)
    # takes the place of platform_sdl.cpp (the window, events, clocks, cursor, mouse and clipboard on SDL), of seams.cpp
    # (its overlay, its file write watch, the isolated run) and of the sockets (its sockets on 127.0.0.1 and a pair's
    # network). Its own calls of the system are tests/verify/system's, one file per system.
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp tests/verify/image/*.cpp tests/verify/platform/*.cpp
         tests/verify/system/*.cpp)
    if(WIN32)
        list(FILTER EI_HARNESS_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
    else()
        list(FILTER EI_HARNESS_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
    endif()
    target_sources(ei-rebuild PRIVATE ${EI_HARNESS_SOURCES})
    if(MSVC)
        # The harness reads its scenario lines with the standard sscanf on every system, not MSVC's sscanf_s.
        set_property(SOURCE ${EI_HARNESS_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
    endif()
    target_include_directories(ei-rebuild PRIVATE tests/verify)
    target_link_libraries(ei-rebuild PRIVATE SDL3::SDL3-static)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
    if(EI_COVERAGE)
        set(EI_COVERAGE_OPTIONS /fsanitize-coverage=inline-bool-flag /Gh)
        foreach(target ei_engine ei_game ei_render ei_platform)
            target_compile_options(${target} PRIVATE ${EI_COVERAGE_OPTIONS})
        endforeach()
        set_property(SOURCE src/main.cpp ${EI_PROGRAM_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${EI_COVERAGE_OPTIONS})
        target_sources(ei-rebuild PRIVATE tests/verify/harness_coverage_entry.asm)
        set_property(SOURCE tests/verify/harness_coverage.cpp APPEND PROPERTY COMPILE_DEFINITIONS EI_COVERAGE)
    endif()
endif()

# The settings checks read the expected new settings file (tests/unit/settings_defaults.ini) from the source tree.
set_property(SOURCE tests/unit/settings_file.cpp APPEND PROPERTY COMPILE_DEFINITIONS "EI_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
# The data effects' checks read the effects the notes and the modding guide write out (docs/notes/effects.md).
set_property(SOURCE tests/unit/particle_record.cpp APPEND PROPERTY COMPILE_DEFINITIONS "EI_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

if(WIN32)
    # The game is a windowed program whose entry is main.
    target_link_options(ei-rebuild PRIVATE /ENTRY:mainCRTStartup)

    # The unit checks (tests/unit): the settings file's format, lib::HashMap's walk order, the .ini reader,
    # lib::ParseIpv4, lib/maths.h, the save pictures, the bounded text reads, the tagged stream blocks and the byte
    # buffers with their cursor and coder, the mod's path map, the weather's particle field, the TOML writer and reader,
    # the PNG textures, the glTF figure models, the script floats through Lua, the spells' behaviours, the data
    # effects' records and their server inputs, without the
    # game. build.cmd runs it.
    add_executable(ei_unit_check tests/unit/unit_check.cpp tests/unit/settings_file.cpp tests/unit/hash_map.cpp
                   tests/unit/ini_file.cpp tests/unit/ipv4.cpp tests/unit/maths.cpp tests/unit/save_picture.cpp
                   tests/unit/audio_mix.cpp tests/unit/display_sizes.cpp tests/unit/bounded_text.cpp
                   tests/unit/tagged_stream.cpp tests/unit/game_buffer.cpp tests/unit/platform_text.cpp
                   tests/unit/path_layers.cpp tests/unit/mod_hash.cpp src/engine/mods/mod_hash.cpp
                   tests/unit/weather_field.cpp tests/unit/toml.cpp src/lib/toml.cpp
                   tests/unit/texture_png.cpp src/engine/models/texture_png.cpp tests/unit/map_folder.cpp
                   src/engine/data/map_folder.cpp src/lib/png.cpp src/game/effects/weather_field.cpp src/engine/core/buffer.cpp src/engine/core/coder.cpp
                   tests/unit/figure_gltf.cpp src/engine/models/figure_gltf.cpp src/engine/models/figure_gltf_animation.cpp src/lib/json.cpp src/lib/vecmath.cpp tests/unit/lua_number.cpp
                   tests/unit/particle_record.cpp src/engine/particles/particle_expr.cpp src/engine/particles/particle_record.cpp src/engine/core/random.cpp src/lib/crt.cpp
                   tests/unit/effect_inputs.cpp src/game/effects/effect_inputs.cpp src/game/net/net_delta.cpp
                   src/audio/audio.cpp src/game/app/settings_document.cpp src/engine/core/core_streams.cpp src/engine/data/ini_file.cpp
                   src/game/saves/save_picture.cpp src/lib/qoi.cpp src/engine/core/asset_text.cpp src/lib/hash_map.cpp src/lib/ipv4.cpp
                   src/lib/text_case.cpp src/lib/calendar.cpp src/lib/str.cpp src/lib/crt_format.cpp
                   tests/unit/spell_behaviour.cpp src/game/spells/spell_behaviour.cpp
                   ${EI_PROGRAM_SOURCES} ${EI_GAME_CALLS} ${EI_SDL_CALLS})
    target_include_directories(ei_unit_check PRIVATE src third_party)
    target_link_libraries(ei_unit_check PRIVATE ei_platform tomlc17 stb_dxt wuffs cgltf eris SDL3::SDL3-static miniz)

    # Offline check of the interface text layout (render_text): DrawText rules on synthetic cases, and a
    # report against GDI on the game's strings (tools/text_check; GDI only there).
    add_executable(ei_text_check tools/text_check/text_check.cpp tools/text_check/titles_check.cpp)
    target_include_directories(ei_text_check PRIVATE src)
    target_link_libraries(ei_text_check PRIVATE ei_render gdi32 user32 shell32)
    # Offline check of the SDL backend's keys (src/platform/sdl/sdl_keys.cpp, ticket PL9a) against the key codes and control
    # characters Windows' layouts give, for every layout the machine has.
    add_executable(ei_input_check tools/input_check/input_check.cpp src/platform/sdl/sdl_keys.cpp)
    target_include_directories(ei_input_check PRIVATE src)
    target_link_libraries(ei_input_check PRIVATE SDL3::SDL3-static user32)
    # Offline check of the interface canvases (render_ui, ticket N1) on the GPU (gpu.h's device): every command against
    # a CPU reference, and fills, frames, lines and pixels against GDI drawing into a DIB section (GDI only there).
    add_executable(ei_ui_check tools/ui_check/ui_check.cpp tests/render/glyph_cases.cpp)
    target_include_directories(ei_ui_check PRIVATE src tests)
    target_link_libraries(ei_ui_check PRIVATE ei_render gdi32 user32)
    # Offline check of the performance overlay (render_perf, ticket R21) on the GPU: its text and its pixels over a
    # target.
    add_executable(ei_perf_check tools/perf_check/perf_check.cpp)
    target_include_directories(ei_perf_check PRIVATE src)
    target_link_libraries(ei_perf_check PRIVATE ei_render)
else()
    # The POSIX checks (ctest; Linux without a display: xvfb-run ctest): the POSIX platform's (paths, files, the .ini
    # reader, the one-instance lock, sockets, the settings document, IPv4 text, the maths functions, the Bink container,
    # the data effects' records;
    # tests/port/port_check.cpp), the interface's glyph cases on Vulkan or Metal (tests/render/glyph_cases.cpp,
    # as tools/ui_check runs them on Direct3D 12), the game window's pixels and the cursor's hold, and on Linux the
    # window's present after resizes, read back from the X server.
    enable_testing()
    add_executable(ei_port_check tests/port/port_check.cpp src/lib/text_case.cpp src/lib/calendar.cpp src/lib/ipv4.cpp src/lib/str.cpp src/lib/crt_format.cpp
                   src/game/app/settings_document.cpp tests/unit/settings_file.cpp tests/unit/ini_file.cpp
                   tests/unit/ipv4.cpp tests/unit/maths.cpp tests/unit/save_picture.cpp tests/unit/audio_mix.cpp
                   tests/unit/display_sizes.cpp tests/unit/path_layers.cpp tests/unit/mod_hash.cpp src/engine/mods/mod_hash.cpp tests/unit/weather_field.cpp tests/unit/toml.cpp src/lib/toml.cpp tests/unit/texture_png.cpp src/engine/models/texture_png.cpp src/game/effects/weather_field.cpp src/audio/audio.cpp src/engine/data/ini_file.cpp src/engine/core/asset_text.cpp src/game/saves/save_picture.cpp src/lib/qoi.cpp src/video/bink_file.cpp
                   tests/unit/map_folder.cpp src/engine/data/map_folder.cpp src/lib/png.cpp
                   tests/unit/figure_gltf.cpp src/engine/models/figure_gltf.cpp src/engine/models/figure_gltf_animation.cpp src/lib/json.cpp src/lib/vecmath.cpp tests/unit/lua_number.cpp
                   tests/unit/spell_behaviour.cpp src/game/spells/spell_behaviour.cpp
                   tests/unit/particle_record.cpp src/engine/particles/particle_expr.cpp src/engine/particles/particle_record.cpp src/engine/core/random.cpp src/lib/crt.cpp
                   tests/unit/effect_inputs.cpp src/game/effects/effect_inputs.cpp src/game/net/net_delta.cpp src/engine/core/core_streams.cpp
                   ${EI_PROGRAM_SOURCES} ${EI_GAME_CALLS} ${EI_SDL_CALLS})
    target_include_directories(ei_port_check PRIVATE src third_party)
    target_link_libraries(ei_port_check PRIVATE ei_platform tomlc17 stb_dxt wuffs cgltf eris SDL3::SDL3-static miniz)
    add_test(NAME port_platform COMMAND ei_port_check)
    # The mission scripts' Lua bindings on a small world of their own (tools/convert/lua_check.cpp).
    add_test(NAME port_lua COMMAND ei-convert --lua-check)
    # docs/modding.md's Lua API tables and examples/lua_api/ei_api.lua against the declarations (tools/convert/lua_api.h).
    add_test(NAME port_lua_api COMMAND ei-convert --lua-api-docs ${CMAKE_SOURCE_DIR} --check)
    # The original particle types against their recorded C++ runs (tools/convert/effect_check.cpp).
    add_test(NAME port_effects COMMAND ei-convert --effect-check)
    add_executable(ei_render_check tests/port/render_check.cpp tests/render/glyph_cases.cpp ${EI_PROGRAM_SOURCES}
                   ${EI_GAME_CALLS} ${EI_SDL_CALLS})
    target_include_directories(ei_render_check PRIVATE src tests ${CMAKE_BINARY_DIR})
    target_link_libraries(ei_render_check PRIVATE ei_game ei_engine SDL3::SDL3-static)
    # SDL3 comes again through ei_game's static libraries (their link-only dependencies); Apple's linker warns.
    if(APPLE)
        target_link_options(ei_render_check PRIVATE LINKER:-no_warn_duplicate_libraries)
    endif()
    if(NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(ei_render_check PRIVATE X11::X11)
    endif()
    add_test(NAME port_render COMMAND ei_render_check)
    if(NOT APPLE)
        # The POSIX platform's keyboard (LayoutKey, LayoutControlCharacter, KeyName on SDL's keymap) on X11's us, ru,
        # fr and de layouts, set with setxkbmap on an Xvfb of its own (tools/input_check/input_check_linux.cpp).
        add_executable(ei_input_check tools/input_check/input_check_linux.cpp src/lib/text_case.cpp src/lib/calendar.cpp
                       ${EI_PROGRAM_SOURCES} ${EI_GAME_CALLS} ${EI_SDL_CALLS})
        target_include_directories(ei_input_check PRIVATE src)
        target_link_libraries(ei_input_check PRIVATE ei_platform SDL3::SDL3-static)
        add_test(NAME port_input COMMAND ei_input_check)
        set_tests_properties(port_input PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    endif()
    # The HLSL programs and src/render/gpu.cpp on Vulkan (Linux; lavapipe without a GPU) or Metal (macOS).
    add_test(NAME port_shaders COMMAND ei_shader_check --require)
    add_test(NAME port_gpu COMMAND ei_gpu_check --require)
    set_tests_properties(port_platform port_lua port_render port_shaders port_gpu PROPERTIES TIMEOUT 60)
endif()
