# The original effects on data (assets/effects/<Name>.effect.toml) written as one C++ header of byte arrays and a
# table by name, built into the executable (particle_data.cpp's registry reads them below the mod layers;
# docs/notes/effects.md, "The original effects on data"). CMakeLists.txt runs it at build time.
# cmake -DFOLDER=<assets/effects> -DOUT=<header> -P embed_effects.cmake
file(GLOB files ${FOLDER}/*.effect.toml)
list(SORT files)
set(arrays "")
set(table "")
set(index 0)
foreach(path ${files})
    file(READ ${path} hex HEX)
    string(REGEX REPLACE "(................................................................)" "\\1\n" hex "${hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    get_filename_component(file ${path} NAME)
    string(REGEX REPLACE "\\.effect\\.toml$" "" name ${file})
    string(APPEND arrays "constexpr unsigned char kFile${index}[] = {\n${bytes}\n};\n")
    string(APPEND table "    {\"${name}\", kFile${index}, sizeof(kFile${index})},\n")
    math(EXPR index "${index} + 1")
endforeach()
file(WRITE ${OUT} "// assets/effects (${index} files) written by src/game/effects/embed_effects.cmake.\n"
                  "#pragma once\n\nnamespace builtin_effects {\n\n"
                  "struct File {\n    const char* name;   // the file's stem\n"
                  "    const unsigned char* bytes;\n    unsigned size;\n};\n\n"
                  "${arrays}\nconstexpr File kFiles[] = {\n${table}};\n\n}   // namespace builtin_effects\n")
