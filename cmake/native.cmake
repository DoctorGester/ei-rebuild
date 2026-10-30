# The native build off Windows (docs/notes/macos.md): the game, SDL3 and the POSIX platform, and on macOS the renderer
# on sokol's Metal backend (src/platform/graphics_metal.mm). EI_PORT_CHECK_ONLY builds the game and the platform
# without the renderer (also the starting point for Linux, which has no graphics backend yet).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EI_ASSET_CODEPAGE 1251 CACHE STRING "The code page of the assets', the saves' and the network's text")
add_compile_definitions(EI_ASSET_CODEPAGE=${EI_ASSET_CODEPAGE})
# Each written floating-point operation kept: ARM otherwise contracts a multiply and an add into one FMA.
add_compile_options(-ffp-contract=off -fno-fast-math)
option(EI_PORT_CHECK_ONLY "Build the game and the platform without the renderer" OFF)
include(cmake/port-check.cmake)
file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)
add_library(ei_game STATIC ${EI_GAME_SOURCES})
target_include_directories(ei_game PRIVATE include src)
if(EI_PORT_CHECK_ONLY)
    return()
endif()
if(NOT APPLE)
    message(FATAL_ERROR "The native renderer runs on Metal (macOS) only; configure with -DEI_PORT_CHECK_ONLY=ON.")
endif()

enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES} src/platform/graphics_metal.mm src/platform/floating_point.cpp)
target_compile_definitions(ei_render PRIVATE SOKOL_METAL)
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol)
target_link_libraries(ei_render PRIVATE SDL3::SDL3-static "-framework Metal" "-framework QuartzCore" "-framework Foundation")
set_source_files_properties(src/platform/graphics_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
include(cmake/shaders.cmake)

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

# The Rebirth install's profile (CMakeLists.txt, the same lines).
file(STRINGS ${CMAKE_SOURCE_DIR}/config/rebirth_profile.ini EI_REBIRTH_PROFILE_LINES REGEX "^[A-Za-z]+ = true$")
set(EI_REBIRTH_PROFILE none)
foreach(line ${EI_REBIRTH_PROFILE_LINES})
    string(REGEX REPLACE " = true$" "" name ${line})
    list(APPEND EI_REBIRTH_PROFILE ${name})
endforeach()
list(JOIN EI_REBIRTH_PROFILE "," EI_REBIRTH_PROFILE)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/config/rebirth_profile.inc CONTENT "\"@EI_REBIRTH_PROFILE@\"\n" @ONLY)

# The game: an app bundle (src/standalone/posix/host.cpp; data from EI_GAME_ROOT, else beside the executable).
set(EI_NATIVE_STARTUP src/hook/log.cpp src/hook/rebirth_hook.cpp src/standalone/startup.cpp
    src/standalone/static_init.cpp src/standalone/static_exit.cpp)
add_executable(ei-rebuild src/standalone/posix/host.cpp ${EI_NATIVE_STARTUP})
set_target_properties(ei-rebuild PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.eirebuild.game MACOSX_BUNDLE_BUNDLE_NAME "Evil Islands")
target_include_directories(ei-rebuild PRIVATE include src ${CMAKE_BINARY_DIR}/config)
target_link_libraries(ei-rebuild PRIVATE ei_render ei_platform "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")

# The interface canvases' glyph cases on Metal (tests/render/glyph_cases.cpp, as tools/ui_check runs them on
# Direct3D 11).
add_executable(ei_render_check tests/port/render_check.cpp tests/render/glyph_cases.cpp ${EI_NATIVE_STARTUP})
target_include_directories(ei_render_check PRIVATE include src tests third_party/sokol ${CMAKE_BINARY_DIR}/config)
target_link_libraries(ei_render_check PRIVATE ei_render ei_platform "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
add_test(NAME port_render COMMAND ei_render_check)
set_tests_properties(port_render PROPERTIES TIMEOUT 60)
