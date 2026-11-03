# The SDL_GPU spike (tools/gpu_spike/README.md), included by CMakeLists.txt with -DEI_GPU_SPIKE=ON only.
# - The renderer records its sokol_gfx calls (record.cpp, sokol's trace hooks) and the harness's capture step writes a
#   frame dump (Windows, Direct3D 11).
# - ei_gpu_spike draws a dump again through sokol_gfx (the platform's backend: Direct3D 11 / OpenGL) or SDL_GPU
#   (Direct3D 12 / Vulkan), with the programs of the world's passes (terrain, mesh, solid, the shadow casters and the
#   shadow map's range) as HLSL (tools/gpu_spike/shaders) compiled by
#   SDL_shadercross (the main build's, tools/shader_toolchain) to DXIL, SPIR-V and MSL at build time.

target_compile_definitions(ei_render PRIVATE SOKOL_TRACE_HOOKS EI_GPU_SPIKE)
target_include_directories(ei_render PRIVATE ${CMAKE_SOURCE_DIR}/tools)
if(WIN32)
    target_sources(ei_render PRIVATE tools/gpu_spike/record.cpp tools/gpu_spike/dump.cpp)
endif()
# Plain C file and environment calls (fopen, getenv): the spike's files only.
set_source_files_properties(tools/gpu_spike/record.cpp tools/gpu_spike/dump.cpp tools/gpu_spike/replay.cpp
                            PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
if(EI_HARNESS)
    target_compile_definitions(ei-rebuild PRIVATE EI_GPU_SPIKE)
    target_include_directories(ei-rebuild PRIVATE ${CMAKE_SOURCE_DIR}/tools)
endif()

set(EI_SPIKE_SHADER_DIR ${CMAKE_BINARY_DIR}/spike_shaders)
file(MAKE_DIRECTORY ${EI_SPIKE_SHADER_DIR})
file(GLOB EI_SPIKE_HLSL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/gpu_spike/shaders/*.hlsl
     ${CMAKE_SOURCE_DIR}/tools/gpu_spike/shaders/*.hlsli)
set(EI_SPIKE_FORMATS spirv msl)
if(WIN32)
    list(APPEND EI_SPIKE_FORMATS dxil)
endif()
set(EI_SPIKE_SHADER_HEADERS)
foreach(program terrain mesh solid shadow_caster shadow_terrain shadow_reduce shadow_dilate)
    foreach(stage vertex fragment)
        if(stage STREQUAL "vertex")
            set(entry vs_main)
            set(define -DEI_VERTEX)
        else()
            set(entry ps_main)
            set(define -DEI_FRAGMENT)
        endif()
        foreach(format ${EI_SPIKE_FORMATS})
            set(binary ${EI_SPIKE_SHADER_DIR}/${program}_${stage}.${format})
            set(header ${EI_SPIKE_SHADER_DIR}/${program}_${stage}_${format}.h)
            string(TOUPPER ${format} dest)
            add_custom_command(OUTPUT ${header}
                COMMAND ${EI_SHADERCROSS} ${CMAKE_SOURCE_DIR}/tools/gpu_spike/shaders/${program}.hlsl -s HLSL -d ${dest}
                        -t ${stage} -e ${entry} ${define} -I ${CMAKE_SOURCE_DIR}/tools/gpu_spike/shaders -o ${binary}
                COMMAND ${CMAKE_COMMAND} -DSOURCE=${binary} -DNAME=k_${program}_${stage}_${format} -DOUT=${header}
                        -P ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
                DEPENDS ${EI_SPIKE_HLSL} ${CMAKE_SOURCE_DIR}/src/render/embed.cmake
                COMMENT "shadercross ${program}.hlsl ${stage} to ${format}"
                VERBATIM)
            list(APPEND EI_SPIKE_SHADER_HEADERS ${header})
        endforeach()
    endforeach()
endforeach()

add_executable(ei_gpu_spike tools/gpu_spike/replay.cpp tools/gpu_spike/replay_sokol.cpp
               tools/gpu_spike/replay_sdlgpu.cpp tools/gpu_spike/dump.cpp ${EI_SPIKE_SHADER_HEADERS})
target_include_directories(ei_gpu_spike PRIVATE src third_party/sokol ${EI_SPIKE_SHADER_DIR})
target_link_libraries(ei_gpu_spike PRIVATE ei_render SDL3::SDL3-static)
