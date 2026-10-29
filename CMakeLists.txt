cmake_minimum_required(VERSION 3.24)
project(EvilIslands CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build for 32-bit x86 (game.exe and its DLLs are 32-bit). Use the x86 vcvars environment.")
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
# is the harness build, the same image with the harness linked in (tests/verify: scenario runs of the image) and
# ei_harness.dll, the harness for the original game.exe (EI_NO_HOOKS runs for goldens) with the vector recorders
# (tests/vectors/record_*.cpp, installed only in the original game.exe with EI_VECTORS=1).
option(EI_HARNESS "Build the harness: linked into ei-rebuild.exe, and ei_harness.dll for game.exe" OFF)

# Every string inside the game is UTF-8 (docs/notes/standalone.md section 4, src/game/asset_text.h): asset, save and
# network text is converted from and to EI_ASSET_CODEPAGE where it crosses.
set(EI_ASSET_CODEPAGE 1251 CACHE STRING "The code page of the assets', the saves' and the network's text")
add_compile_definitions(EI_ASSET_CODEPAGE=${EI_ASSET_CODEPAGE})

# x87 floating point where the original's rounding still needs it: SSE rounds differently there.
# /fp:precise (SV7d1a): /fp:fast regrouped sums and folded constants across them, which drifted by
# an ulp (SV7d1). Precise's extra rounding stores change nothing on the main thread, which runs
# at 24-bit precision; it calls the CRT for sqrt/sin/cos/atan2/exp/log, so the x87 code uses x87::.
# The targets with EI_SSE2 set (the renderer and the game code but the x87 list, ei_sse2 below) build for SSE2.
add_compile_options("$<IF:$<BOOL:$<TARGET_PROPERTY:EI_SSE2>>,/arch:SSE2,/arch:IA32>" /fp:precise /W4 /permissive- /utf-8)

# AddressSanitizer (build.cmd asan: the verify build with it, into build\asan; docs/notes/harness.md): every target
# instrumented (the STL's container annotations must match across the libraries), the runtime DLL copied beside the
# executables. Its reports go to the run's folder (tests/verify/image/asan_report.cpp).
option(EI_ASAN "Build with AddressSanitizer (/fsanitize=address)" OFF)
if(EI_ASAN)
    add_compile_options(/fsanitize=address)
    get_filename_component(EI_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(COPY "${EI_CL_DIR}/clang_rt.asan_dynamic-i386.dll" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# The game code (src/game, src/lib) builds for SSE2 but the files listed here (C30a, C30e; docs/notes/cleanup-tooling.md
# "SSE2 build", the plan and why each group is still x87). Their floating point reaches game state or the goldens
# through maths that rounds at the main thread's 24-bit precision in ways plain float maths does not give yet: double
# registers (D(), double locals), double constants wider than a float, transcendentals (x87::fsin / fexp, the game
# CRT's pow / acos), the terrain's chop rounding; picking reads the camera's, the view's and the projected meshes'.
# Each group below is the ticket that moves it with the goldens unchanged; a file split from one of these joins it here.
set(EI_X87_GAME_SOURCES
    # C30e2: scripts
    game/script_addon game/script_addon_commands game/script_addon_math game/script_addon_units game/script_effects
    game/script_eval game/script_exec game/script_exec_helpers game/script_exec_run game/script_parse
    # C30e3: items, spells and world objects
    game/base_manager game/hoover_loot game/item game/item_manager game/item_stock game/lever_object
    game/lever_table_load game/light game/magic_trap game/map_spec game/map_spec_effect game/material game/spell
    game/spell_cast game/spell_effect game/torch_client_spec game/vis_fog_manager
    # C30e4: the transcendental helpers and the spell effects
    game/exit_area game/spell_effect_start game/spell_effect_update
    # C30e5: the server world and world effects
    game/world_effect game/world_effect_missile game/world_effect_moshka game/world_effect_sequence
    game/world_effect_tornado game/world_effect_visual game/world_game game/world_glue game/world_party
    game/world_quests game/world_rules game/world_save game/world_server game/world_spells game/world_sync
    game/world_tick game/world_time game/world_unit_set_add game/world_units game/world_update game/world_weather
    # C30e6: units
    game/unit game/unit_armour_layers game/unit_create_stats game/unit_figure_queries game/unit_hits game/unit_lever
    game/unit_motion_tick game/unit_params_protection game/unit_params_skill game/unit_points game/unit_record
    game/unit_size_class game/unit_skill_price game/unit_skins game/unit_stat_small game/unit_stats game/unit_stats_pack
    game/unit_update game/unit_vision game/unit_vitals
    # C30e7: combat
    game/combat game/combat_actions game/combat_motion game/combat_orders game/combat_planning game/combat_sight
    game/combat_spell_sight game/combat_steps game/combat_taming game/combat_tick game/combat_update
    # C30e8: AI maps, paths and motion
    game/ai_block_search game/ai_events game/ai_footprint game/ai_footprint_shapes game/ai_line_step game/ai_map
    game/ai_map_load game/ai_motion game/ai_movement game/ai_path game/ai_path_smooth game/ai_path_stand game/ai_plan
    game/ai_refresh game/ai_shape_cache game/ai_unit_shape game/ai_water_patch
    # C30e9: unit AI
    game/unit_ai game/unit_ai_modes game/unit_ai_senses game/unit_ai_targets
    # C30e10: geometry and figures
    game/core_maths game/core_transform game/core_transform_math game/figure_bones game/figure_drawer game/figure_mesh
    game/figure_mesh_file game/figure_part_bounds game/figure_part_draw game/figure_part_draw_plain game/link_figure
    game/link_loader game/link_mesh_draw game/maths game/matrix_math game/model game/model_db game/object3d_figure_setup
    game/object3d_halves game/object_maths game/projection game/quaternion game/skin_quaternion game/view_matrices
    # C30e11: map objects
    game/map_object game/map_object_animation game/map_object_animation_bones game/map_object_animation_state
    game/map_object_cage game/map_object_client_effects game/map_object_companion game/map_object_draw
    game/map_object_figures game/map_object_parts
    # C30e12: client units and world
    game/unit_client_animation game/unit_client_attack game/unit_client_blink game/unit_client_effects
    game/unit_client_models game/unit_client_path game/unit_client_update game/unit_client_wounds game/world_client
    game/world_client_draw game/world_client_maps game/world_client_setup game/world_client_view
    # C30e13: camera, picking and the briefing
    game/camera game/camera_motion game/camera_path_load game/device_bounds game/device_mesh game/ui_battle_commands
    game/ui_battle_hover game/ui_briefing_create game/ui_briefing_dialogue game/ui_briefing_input
    game/ui_briefing_people game/ui_briefing_phrases game/ui_briefing_start game/ui_camera_input game/ui_camera_keys
    game/ui_global_map game/ui_global_map_input game/ui_global_map_paths game/ui_global_map_places game/ui_hud
    game/ui_hud_map game/ui_view_screen
    # C30e14: terrain
    game/sector_map game/sector_spiral game/terrain game/terrain_landscape game/terrain_sector
    # C30e15: the main loop and the game CRT
    game/app_main lib/crt_x87)
list(TRANSFORM EI_X87_GAME_SOURCES REPLACE "(.+)" "${CMAKE_SOURCE_DIR}/src/\\1.cpp")
file(GLOB EI_SSE2_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)
list(REMOVE_ITEM EI_SSE2_GAME_SOURCES ${EI_X87_GAME_SOURCES})

# The new renderer (docs/notes/renderer.md): sokol_gfx on Direct3D 11, switched on with EI_RENDER.
# The harness build turns on sokol's validation layer (messages go to ei-rebuild.log).
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES})
set_target_properties(ei_render PROPERTIES EI_SSE2 ON)
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol)
target_link_libraries(ei_render PUBLIC d3d11 dxgi)
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
foreach(part fonts/README.txt fonts/LICENSE sokol/README.txt sokol/LICENSE stb/README.txt)
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
    src/standalone/image_data.cpp
    src/standalone/image_imports.cpp
    src/standalone/standalone_main.cpp
    src/standalone/startup.cpp
    src/standalone/static_exit.cpp
    src/standalone/static_init.cpp
    ${EI_X87_GAME_SOURCES})
set_target_properties(ei-rebuild PROPERTIES WIN32_EXECUTABLE ON)
target_include_directories(ei-rebuild PRIVATE include src)
target_link_libraries(ei-rebuild PRIVATE ei_render)
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
# The initial bytes of the image's own copy of game.exe's .rdata and .data (src/standalone/image_data.cpp, S18d), written
# at build time into image/ in the build folder (not committed; tools/gen_image_cells.py --bytes): zero but the
# committed pointer cells. The build reads no game.exe (C25c).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(EI_IMAGE_DATA ${CMAKE_BINARY_DIR}/image/image_data_bytes.inc)
add_custom_command(OUTPUT ${EI_IMAGE_DATA}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_image_cells.py --bytes ${EI_IMAGE_DATA}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_image_cells.py ${CMAKE_SOURCE_DIR}/config/named_data.csv
            ${CMAKE_SOURCE_DIR}/config/vtables.csv
            ${CMAKE_SOURCE_DIR}/src/standalone/image_cells.inc
    COMMENT "Writing the image's .rdata and .data bytes"
    VERBATIM)
target_sources(ei-rebuild PRIVATE ${EI_IMAGE_DATA})
set_source_files_properties(src/standalone/image_data.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/image
                            OBJECT_DEPENDS ${EI_IMAGE_DATA})
# The Rebirth install's profile, which an exe dropped into a Rebirth folder without an ei_rebirth.ini takes
# (src/standalone/startup.cpp): config/ei_rebirth_standalone.ini's flags as a spec (rebirth::ApplySpec), written at
# configure time into config/rebirth_profile.inc in the build folder.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/config/ei_rebirth_standalone.ini)
file(STRINGS ${CMAKE_SOURCE_DIR}/config/ei_rebirth_standalone.ini EI_REBIRTH_PROFILE_LINES REGEX "^[A-Za-z]+=[01]$")
set(EI_REBIRTH_PROFILE)
foreach(line ${EI_REBIRTH_PROFILE_LINES})
    string(REGEX MATCH "^([A-Za-z]+)=([01])$" unused ${line})
    if(CMAKE_MATCH_1 STREQUAL "Default")
        if(CMAKE_MATCH_2 STREQUAL "0")
            list(APPEND EI_REBIRTH_PROFILE none)
        else()
            list(APPEND EI_REBIRTH_PROFILE all)
        endif()
    elseif(NOT CMAKE_MATCH_1 MATCHES "^(NPlayers|Pause)$")
        list(APPEND EI_REBIRTH_PROFILE ${line})
    endif()
endforeach()
list(JOIN EI_REBIRTH_PROFILE "," EI_REBIRTH_PROFILE)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/config/rebirth_profile.inc CONTENT "\"@EI_REBIRTH_PROFILE@\"\n" @ONLY)
set_source_files_properties(src/standalone/startup.cpp PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/config)
if(EI_HARNESS)
    # The harness (tests/verify; docs/notes/harness.md): its shared part, with what it names of the hook framework's
    # game.exe side (src/hook/hook.cpp, cover.cpp) and of the vector recordings (vectors_record.cpp: the report
    # flushes them), built once into both of its homes: linked into this exe with the image's side (tests/verify/image,
    # which calls our code) for the image's runs, and into ei_harness.dll below for game.exe's.
    file(GLOB EI_HARNESS_SOURCES CONFIGURE_DEPENDS tests/verify/*.cpp)
    add_library(ei_harness_shared OBJECT
        ${EI_HARNESS_SOURCES} src/hook/hook.cpp src/hook/cover.cpp tests/vectors/vectors_record.cpp)
    target_include_directories(ei_harness_shared PRIVATE include src)
    file(GLOB EI_HARNESS_IMAGE_SOURCES CONFIGURE_DEPENDS tests/verify/image/*.cpp)
    target_sources(ei-rebuild PRIVATE $<TARGET_OBJECTS:ei_harness_shared> ${EI_HARNESS_IMAGE_SOURCES})
endif()
# The game code built for SSE2 (EI_SSE2_GAME_SOURCES above): a library, whole (every object, as if listed),
# whose objects the linker takes after all of the exe's own. An inline function that files of both kinds keep out of
# line is taken from the first object that defines it, so the x87 code always gets its x87 copy.
add_library(ei_sse2 STATIC ${EI_SSE2_GAME_SOURCES})
set_target_properties(ei_sse2 PROPERTIES EI_SSE2 ON)
target_include_directories(ei_sse2 PRIVATE include src)
target_link_libraries(ei-rebuild PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_sse2>")
# The NaN audit's log (src/lib/nan_audit.h) in the verify and debug builds.
target_compile_definitions(ei-rebuild PRIVATE $<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)
target_compile_definitions(ei_sse2 PRIVATE $<$<OR:$<BOOL:${EI_HARNESS}>,$<CONFIG:Debug>>:EI_NAN_AUDIT_LOG>)
# /OPT:REF: code and data nothing references are left out (8.2 MB -> 5.1 MB, mostly the per-file copies of
# gameapi.h's initialisers and unused library code); the static objects' initialisers stay, as .CRT$XCU references
# them. No /OPT:ICF: folding identical functions would give two functions one address, and a comparison of function
# pointers would then change its answer. The debug build keeps the linker's default (no /OPT:REF with /INCREMENTAL).
# /MAP: ei-rebuild.map lets the harness crash log (ei-rebuild+offset) be resolved to functions.
target_link_options(ei-rebuild PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:NOICF>" /MAP)

if(EI_HARNESS)
    # ei_harness.dll: the harness in game.exe, the original game, for the references (goldens, function vectors,
    # widescreen frames): ei_launch injects it before game.exe's own code runs (docs/notes/standalone.md section 5).
    # The shared harness and the hook framework, game.exe's data seam (shift 0), the Rebirth flags the harness reads,
    # the image side's absence (tests/verify/original) and the vector recorders (tests/vectors/record_*.cpp,
    # installed with EI_VECTORS=1). None of the game: game.exe's side of the harness calls only game.exe's code (H30).
    file(GLOB EI_RECORD_SOURCES CONFIGURE_DEPENDS tests/vectors/record_*.cpp)
    add_library(ei_harness SHARED
        $<TARGET_OBJECTS:ei_harness_shared>
        src/hook/dllmain.cpp
        src/hook/log.cpp
        src/hook/original_data.cpp
        src/hook/paths.cpp
        src/hook/profile.cpp
        src/hook/rebirth_hook.cpp
        src/hook/standalone.cpp
        src/lib/rebirth.cpp
        tests/verify/original/image_absent.cpp
        ${EI_RECORD_SOURCES})
    target_include_directories(ei_harness PRIVATE include src)
    target_link_options(ei_harness PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:NOICF>" /MAP)
endif()

add_executable(ei_launch tools/launcher/launch.cpp)

# Replays the recorded function vectors (tests/vectors/vectors.h) against our code without the game:
# the families' sources, their checks, and a stub of ei::Standalone. build.cmd runs it. The game files are built as
# the game builds them: those of EI_X87_GAME_SOURCES in the executable for x87, the others for SSE2 in a library
# linked whole after its objects (as ei_sse2, so the x87 code keeps its x87 copies of the inline functions).
file(GLOB EI_REPLAY_SOURCES CONFIGURE_DEPENDS tests/vectors/replay_*.cpp)
set(EI_VECTORS_GAME_SOURCES
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
    src/game/game_buffer.cpp
    src/game/maths.cpp
    src/game/matrix_math.cpp
    src/game/net_coder.cpp
    src/game/quaternion.cpp
    src/game/random.cpp
    src/game/script_blocks.cpp
    src/game/script_construct.cpp
    src/game/script_group.cpp
    src/game/script_load.cpp
    src/game/script_parse.cpp
    src/game/script_parse_load.cpp
    src/game/script_reset.cpp
    src/game/script_runtime_helpers.cpp
    src/game/unit_db_globals.cpp
    src/game/options_construct.cpp
    src/game/script_vars.cpp
    src/game/sector_spiral.cpp
    src/game/settings_document.cpp
    src/game/unit_hit_location.cpp
    src/game/unit_params_protection.cpp
    src/game/unit_params_skill.cpp
    src/game/unit_skill_price.cpp
    src/game/unit_stats.cpp
    src/lib/crt.cpp
    src/lib/crt_format.cpp
    src/lib/file_io.cpp
    src/lib/hash_map.cpp
    src/lib/rebirth.cpp
    src/lib/str.cpp
    src/lib/text_case.cpp)
list(TRANSFORM EI_VECTORS_GAME_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")
set(EI_VECTORS_SSE2_SOURCES ${EI_VECTORS_GAME_SOURCES})
list(REMOVE_ITEM EI_VECTORS_SSE2_SOURCES ${EI_X87_GAME_SOURCES})
set(EI_VECTORS_X87_SOURCES ${EI_VECTORS_GAME_SOURCES})
list(REMOVE_ITEM EI_VECTORS_X87_SOURCES ${EI_VECTORS_SSE2_SOURCES})
# EI_VECTORS_REPLAY: the game's data at fixed addresses comes from the checks' stand-in (src/game/game_data.h).
add_library(ei_vectors_sse2 STATIC ${EI_VECTORS_SSE2_SOURCES})
set_target_properties(ei_vectors_sse2 PROPERTIES EI_SSE2 ON)
target_include_directories(ei_vectors_sse2 PRIVATE include src)
target_compile_definitions(ei_vectors_sse2 PRIVATE EI_VECTORS_REPLAY)
add_executable(ei_vectors ${EI_REPLAY_SOURCES} tests/vectors/hook_stub.cpp ${EI_VECTORS_X87_SOURCES})
target_include_directories(ei_vectors PRIVATE include src)
target_compile_definitions(ei_vectors PRIVATE EI_VECTORS_DATA="${CMAKE_SOURCE_DIR}/tests/vectors/data" EI_VECTORS_REPLAY)
target_link_libraries(ei_vectors PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_vectors_sse2>")

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
target_compile_definitions(ei_launch PRIVATE UNICODE _UNICODE)

