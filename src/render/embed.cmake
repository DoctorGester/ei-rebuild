# A file's bytes written as a C++ array, for files built into the executable (CMakeLists.txt runs it at build time for
# the interface fonts, render_fonts.cpp).
# cmake -DSOURCE=<file> -DNAME=<array> -DOUT=<header> -P embed.cmake
file(READ ${SOURCE} hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "(................................................................)" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(file ${SOURCE} NAME)
file(WRITE ${OUT} "// ${file} (${size} bytes) written by src/render/embed.cmake.\n"
                  "const unsigned char ${NAME}[${size}] = {\n${bytes}\n};\n")
