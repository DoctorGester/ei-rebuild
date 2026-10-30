# One authored source set and compiler for every graphics backend. Generated code stays in the build tree.
find_program(EI_SHDC sokol-shdc HINTS ${CMAKE_SOURCE_DIR}/build/tools REQUIRED)
file(GLOB EI_SHADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/shaders/*.glsl)
set(shader_header ${CMAKE_BINARY_DIR}/shaders/shaders.h)
set(shader_bytecode)
if(WIN32)
    set(shader_bytecode --bytecode)
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
add_custom_command(OUTPUT ${shader_header}
    COMMAND ${EI_SHDC} --input ${CMAKE_SOURCE_DIR}/src/render/shaders/shaders.glsl
            --output ${shader_header} --slang hlsl5:metal_macos:glsl430 --format sokol_impl
            --ifdef --no-log-cmdline ${shader_bytecode}
    DEPENDS ${EI_SHADER_SOURCES} ${EI_SHDC}
    COMMENT "Compiling shaders with sokol-shdc" VERBATIM)
target_sources(ei_render PRIVATE ${shader_header})
target_include_directories(ei_render PUBLIC ${CMAKE_BINARY_DIR})
