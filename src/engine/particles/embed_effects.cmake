# A folder of the game's own data files written as one C++ header of byte arrays and a table by name, built into the
# executable: the original effects (assets/effects/<Name>.effect.toml; particle_data.cpp's registry reads them below
# the mod layers; docs/notes/effects.md, "The original effects on data") and the spells on scripts (assets/spells:
# their .lua files and the records' spells.sdb.toml; docs/notes/items_spells.md, "Scripted spells"). CMakeLists.txt
# runs it at build time.
# cmake -DFOLDER=<folder> -DLABEL=<its name in the source> -DOUT=<header> -DNAMESPACE=<namespace> [-DSUFFIX=<suffix>]
#       -P embed_effects.cmake
# A file's name in the table is its name without SUFFIX (every file of the folder when SUFFIX is empty).
file(GLOB files ${FOLDER}/*${SUFFIX})
list(SORT files)
string(LENGTH "${SUFFIX}" suffixLength)
set(arrays "")
set(table "")
set(index 0)
foreach(path ${files})
    file(READ ${path} hex HEX)
    string(REGEX REPLACE "(................................................................)" "\\1\n" hex "${hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    get_filename_component(file ${path} NAME)
    string(LENGTH "${file}" nameLength)
    math(EXPR nameLength "${nameLength} - ${suffixLength}")
    string(SUBSTRING "${file}" 0 ${nameLength} name)
    string(APPEND arrays "constexpr unsigned char kFile${index}[] = {\n${bytes}\n};\n")
    string(APPEND table "    {\"${name}\", kFile${index}, sizeof(kFile${index})},\n")
    math(EXPR index "${index} + 1")
endforeach()
file(WRITE ${OUT} "// ${LABEL} (${index} files) written by src/engine/particles/embed_effects.cmake.\n"
                  "#pragma once\n\nnamespace ${NAMESPACE} {\n\n"
                  "struct File {\n    const char* name;\n"
                  "    const unsigned char* bytes;\n    unsigned size;\n};\n\n"
                  "${arrays}\nconstexpr File kFiles[] = {\n${table}};\n\n}   // namespace ${NAMESPACE}\n")
