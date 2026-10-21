# One shader function compiled to bytecode with the Windows SDK's fxc, written as a C++ array for
# render_shaders.cpp (CMakeLists.txt's ei_shader runs it at build time). The flags are the ones sokol's
# _sg_d3d11_compile_shader would give d3dcompiler at run time: column-major matrices, optimisation level 3.
# fxc's warnings fail the build (/WX); its success line is not shown.
# cmake -DFXC=<fxc.exe> -DSOURCE=<.hlsl> -DENTRY=<function> -DPROFILE=<vs_4_0, ...> -DNAME=<array>
#       [-DDEFINES=<A=1;B=2>] -DOUT=<header> -P compile.cmake
set(args /nologo /WX /O3 /Zpc /T ${PROFILE} /E ${ENTRY})
foreach(d IN LISTS DEFINES)
    list(APPEND args /D ${d})
endforeach()
execute_process(COMMAND ${FXC} ${args} /Fo ${OUT}.cso ${SOURCE}
                OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fxc ${SOURCE} (${ENTRY}, ${PROFILE}) failed:\n${err}${out}")
endif()
file(READ ${OUT}.cso hex HEX)
string(REGEX REPLACE "(................................................................)" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUT} "// ${SOURCE} (${ENTRY}, ${PROFILE} ${DEFINES}) compiled by fxc (src/render/shaders/compile.cmake).\n"
                  "const unsigned char ${NAME}[] = {\n${bytes}\n};\n")
