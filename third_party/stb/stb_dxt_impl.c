/* Ours, not stb's: the implementation of stb_dxt.h, built as the stb_dxt library (CMakeLists.txt) without its warnings.
   Used by the PNG textures (src/engine/models/texture_png.cpp; docs/notes/mods.md, "Textures as PNG"). */
#include <string.h> /* stb_dxt.h calls memcpy without including it; clang refuses the implicit declaration */
#define STB_DXT_IMPLEMENTATION
#include "stb/stb_dxt.h"
