# The renderer's shaders (docs/notes/renderer.md, "Shader authoring"): src/render/shaders/*.glsl, one input
# (shaders.glsl) compiled by sokol-shdc for every graphics backend into shaders/shaders.h in the build folder (not
# committed): the uniform blocks' C structs, the bind slots and each program's sokol shader description. On Windows
# the HLSL is compiled to bytecode here, so no shader compiler runs at start.
# sokol-shdc: the EI_SHDC environment variable (a full path; build.cmd sets this machine's), then -DEI_SHDC, then
# build/tools in the checkout, then PATH.
if(DEFINED ENV{EI_SHDC})
    set(EI_SHDC "$ENV{EI_SHDC}" CACHE FILEPATH "The sokol-shdc shader compiler" FORCE)
endif()
find_program(EI_SHDC sokol-shdc HINTS ${CMAKE_SOURCE_DIR}/build/tools REQUIRED)
file(GLOB EI_SHADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/shaders/*.glsl)
set(EI_SHADER_HEADER ${CMAKE_BINARY_DIR}/shaders/shaders.h)
set(EI_SHDC_OPTIONS)
if(WIN32)
    list(APPEND EI_SHDC_OPTIONS --bytecode --errfmt msvc)
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
add_custom_command(OUTPUT ${EI_SHADER_HEADER}
    COMMAND ${EI_SHDC} --input ${CMAKE_SOURCE_DIR}/src/render/shaders/shaders.glsl --output ${EI_SHADER_HEADER}
            --slang hlsl5:metal_macos --format sokol_impl --ifdef --no-log-cmdline ${EI_SHDC_OPTIONS}
    DEPENDS ${EI_SHADER_SOURCES} ${EI_SHDC}
    COMMENT "Compiling the shaders with sokol-shdc"
    VERBATIM)
target_sources(ei_render PRIVATE ${EI_SHADER_HEADER})
target_include_directories(ei_render PUBLIC ${CMAKE_BINARY_DIR})
