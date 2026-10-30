if(NOT APPLE)
    message(FATAL_ERROR "The runnable native renderer currently targets Metal/macOS. Use EI_PORT_CHECK_ONLY for POSIX compile checks.")
endif()
enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
include(cmake/port-check.cmake)
file(GLOB EI_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/lib/*.cpp)
add_library(ei_game STATIC ${EI_GAME_SOURCES})
target_include_directories(ei_game PRIVATE include src)
file(GLOB EI_RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp)
add_library(ei_render STATIC ${EI_RENDER_SOURCES} src/platform/graphics_metal.mm)
target_compile_definitions(ei_render PRIVATE EI_NATIVE_VALIDATION SOKOL_METAL)
target_include_directories(ei_render PRIVATE include src third_party third_party/sokol ${CMAKE_BINARY_DIR})
target_link_libraries(ei_render PRIVATE SDL3::SDL3-static "-framework Metal" "-framework QuartzCore" "-framework Foundation")
set_source_files_properties(src/platform/graphics_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
option(EI_RENDER_CHECKS "Enable the renderer's readback and diagnostic checks" OFF)
if(EI_RENDER_CHECKS)
    target_compile_definitions(ei_render PRIVATE EI_RENDER_VALIDATION)
endif()

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

add_executable(ei-rebuild src/standalone/posix/host.cpp src/standalone/posix/runtime.cpp
    src/standalone/startup.cpp src/standalone/static_init.cpp src/standalone/static_exit.cpp src/hook/rebirth_hook.cpp)
set_target_properties(ei-rebuild PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.eirebuild.game MACOSX_BUNDLE_BUNDLE_NAME "Evil Islands")
target_include_directories(ei-rebuild PRIVATE include src ${CMAKE_BINARY_DIR}/config)
target_link_libraries(ei-rebuild PRIVATE ei_render ei_platform "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
file(STRINGS ${CMAKE_SOURCE_DIR}/config/rebirth_profile.ini EI_PROFILE_LINES REGEX "^[A-Za-z]+ = true$")
set(EI_REBIRTH_PROFILE none)
foreach(line ${EI_PROFILE_LINES})
    string(REGEX REPLACE " = true$" "" name "${line}")
    list(APPEND EI_REBIRTH_PROFILE ${name})
endforeach()
list(JOIN EI_REBIRTH_PROFILE "," EI_REBIRTH_PROFILE)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/config/rebirth_profile.inc CONTENT "\"@EI_REBIRTH_PROFILE@\"\n" @ONLY)

add_executable(ei_render_check tests/port/render_check.cpp src/standalone/posix/runtime.cpp
    src/standalone/startup.cpp src/standalone/static_init.cpp src/standalone/static_exit.cpp src/hook/rebirth_hook.cpp)
target_include_directories(ei_render_check PRIVATE include src third_party/sokol ${CMAKE_BINARY_DIR}/config)
target_link_libraries(ei_render_check PRIVATE ei_render ei_platform "$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>")
if(EI_RENDER_CHECKS)
    target_compile_definitions(ei_render_check PRIVATE EI_RENDER_VALIDATION)
endif()
add_test(NAME port_render COMMAND ei_render_check)
set_tests_properties(port_render PROPERTIES TIMEOUT 60)
