# The shader toolchain (docs/notes/renderer.md, "HLSL and SDL_shadercross"), included by CMakeLists.txt. At configure
# time it fetches SDL_shadercross's sources (no tagged release yet: a pinned commit), SPIRV-Cross (a Vulkan SDK tag)
# and DirectXShaderCompiler's release binaries from their official GitHub archives and releases, each checked against
# its pinned SHA-256, then builds shadercross and spirv-cross once (tools/shader_toolchain/CMakeLists.txt). It sets
# EI_SHADERCROSS and EI_SPIRV_CROSS to the two tools.
#
# Everything goes to EI_THIRD_PARTY: by default build/third_party of the main checkout (git's common folder), so
# every worktree shares one copy, else of this source tree. The build folder's name holds a hash of the pins and of
# the wrapper's CMakeLists.txt, so another pin builds beside it, and a file lock lets builds that configure at the
# same time wait for each other.

set(EI_THIRD_PARTY "$ENV{EI_THIRD_PARTY}" CACHE PATH "The shader toolchain's downloads and builds")
if(NOT EI_THIRD_PARTY)
    execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE common RESULT_VARIABLE failed
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT failed AND EXISTS "${common}")
        get_filename_component(checkout "${common}" DIRECTORY)
        set(EI_THIRD_PARTY "${checkout}/build/third_party" CACHE PATH "" FORCE)
    else()
        set(EI_THIRD_PARTY "${CMAKE_SOURCE_DIR}/build/third_party" CACHE PATH "" FORCE)
    endif()
endif()

# name; url; sha256; the folder it holds
set(EI_SHADERCROSS_PIN
    SDL_shadercross-1ff05bec573988a98ef9e0260b4da44f512b8367.zip
    https://github.com/libsdl-org/SDL_shadercross/archive/1ff05bec573988a98ef9e0260b4da44f512b8367.zip
    f6baca7f53dc4e5cf5981d543ee17d93675242255d84ba34749e349e70945287
    SDL_shadercross-1ff05bec573988a98ef9e0260b4da44f512b8367)
set(EI_SPIRV_CROSS_PIN
    SPIRV-Cross-vulkan-sdk-1.4.363.0.tar.gz
    https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721
    SPIRV-Cross-vulkan-sdk-1.4.363.0)
# DXC has no macOS release: there it is the one the LunarG Vulkan SDK for macOS ships (libdxcompiler.dylib and its
# headers), found at EI_DXC_ROOT (default: the SDK's VULKAN_SDK folder; docs/notes/macos.md).
if(APPLE)
    set(EI_DXC_ROOT "$ENV{VULKAN_SDK}" CACHE PATH
        "DXC on macOS: a folder with include/dxc/dxcapi.h and lib/libdxcompiler.dylib")
    if(NOT EXISTS "${EI_DXC_ROOT}/include/dxc/dxcapi.h" OR NOT EXISTS "${EI_DXC_ROOT}/lib/libdxcompiler.dylib")
        message(FATAL_ERROR "No DXC at EI_DXC_ROOT '${EI_DXC_ROOT}': install the Vulkan SDK for macOS (it sets "
                            "VULKAN_SDK) or pass -DEI_DXC_ROOT=<folder with include/dxc/dxcapi.h and "
                            "lib/libdxcompiler.dylib>")
    endif()
elseif(WIN32)
    set(EI_DXC_PIN
        dxc_2026_09_29.zip
        https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
        ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
        .)
else()
    set(EI_DXC_PIN
        linux_dxc_2026_09_28.x86_x64.tar.gz
        https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz
        96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
        .)
endif()

# `pin` downloaded (when its file is missing or differs) and unpacked into its own folder, named by its hash; `out`
# set to the folder the archive holds.
function(ei_fetch pin out)
    list(GET ${pin} 0 name)
    list(GET ${pin} 1 url)
    list(GET ${pin} 2 sha)
    list(GET ${pin} 3 inside)
    set(archive ${EI_THIRD_PARTY}/downloads/${name})
    string(SUBSTRING ${sha} 0 12 short)
    set(dir ${EI_THIRD_PARTY}/src/${short})
    if(NOT EXISTS ${dir}/unpacked)
        set(have "")
        if(EXISTS ${archive})
            file(SHA256 ${archive} have)
        endif()
        if(NOT have STREQUAL sha)
            message(STATUS "Fetching ${url}")
            file(DOWNLOAD ${url} ${archive} EXPECTED_HASH SHA256=${sha} STATUS status TLS_VERIFY ON)
            list(GET status 0 code)
            if(code)
                message(FATAL_ERROR "Fetching ${url} failed: ${status}")
            endif()
        endif()
        file(REMOVE_RECURSE ${dir})
        file(ARCHIVE_EXTRACT INPUT ${archive} DESTINATION ${dir})
        file(TOUCH ${dir}/unpacked)
    endif()
    set(${out} ${dir}/${inside} PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY ${EI_THIRD_PARTY})
file(LOCK ${EI_THIRD_PARTY}/toolchain.lock GUARD FILE TIMEOUT 1800 RESULT_VARIABLE locked)
if(locked)
    message(FATAL_ERROR "The shader toolchain's lock (${EI_THIRD_PARTY}/toolchain.lock): ${locked}")
endif()
ei_fetch(EI_SHADERCROSS_PIN shadercross_source)
ei_fetch(EI_SPIRV_CROSS_PIN spirv_cross_source)
if(APPLE)
    set(dxc ${EI_DXC_ROOT})
    set(EI_DXC_PIN macos ${EI_DXC_ROOT})   # in the toolchain's key, so another DXC builds beside it
else()
    ei_fetch(EI_DXC_PIN dxc)
endif()

set(wrapper ${CMAKE_CURRENT_LIST_DIR})
file(SHA256 ${wrapper}/CMakeLists.txt wrapper_hash)
string(SHA256 key "${EI_SHADERCROSS_PIN};${EI_SPIRV_CROSS_PIN};${EI_DXC_PIN};${wrapper_hash};${CMAKE_CXX_COMPILER_ID}")
string(SUBSTRING ${key} 0 12 key)
set(tools ${EI_THIRD_PARTY}/shader-toolchain-${key})
if(NOT EXISTS ${tools}/built)
    message(STATUS "Building the shader toolchain into ${tools} (once: a few minutes)")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S ${wrapper} -B ${tools} -G ${CMAKE_GENERATOR}
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DEI_SDL=${CMAKE_SOURCE_DIR}/third_party/SDL
                -DEI_SHADERCROSS_SOURCE=${shadercross_source} -DEI_SPIRV_CROSS=${spirv_cross_source} -DEI_DXC=${dxc}
        RESULT_VARIABLE failed OUTPUT_FILE ${tools}.log ERROR_FILE ${tools}.log)
    if(NOT failed)
        execute_process(COMMAND ${CMAKE_COMMAND} --build ${tools} --target shader_toolchain
                        RESULT_VARIABLE failed OUTPUT_FILE ${tools}.build.log ERROR_FILE ${tools}.build.log)
    endif()
    if(failed)
        # Each log's last 6000 bytes into the configure's output too: CI keeps only that.
        foreach(log ${tools}.log ${tools}.build.log)
            if(EXISTS ${log})
                file(SIZE ${log} size)
                set(offset 0)
                if(size GREATER 6000)
                    math(EXPR offset "${size} - 6000")
                endif()
                file(READ ${log} tail OFFSET ${offset})
                message(STATUS "The end of ${log}:\n${tail}")
            endif()
        endforeach()
        message(FATAL_ERROR "The shader toolchain's build failed: see ${tools}.log and ${tools}.build.log")
    endif()
    file(TOUCH ${tools}/built)
endif()
file(LOCK ${EI_THIRD_PARTY}/toolchain.lock RELEASE)

set(EI_SHADERCROSS ${tools}/shadercross/shadercross${CMAKE_EXECUTABLE_SUFFIX})
set(EI_SPIRV_CROSS ${tools}/spirv-cross/spirv-cross${CMAKE_EXECUTABLE_SUFFIX})
foreach(tool ${EI_SHADERCROSS} ${EI_SPIRV_CROSS})
    if(NOT EXISTS ${tool})
        message(FATAL_ERROR "The shader toolchain has no ${tool}: remove ${tools} to build it again")
    endif()
endforeach()
