/* Ours, not cgltf's: the implementation of cgltf.h, built as the cgltf library (CMakeLists.txt) without its warnings.
   Used by the figure models as glTF (src/engine/models/figure_gltf.cpp; docs/notes/mods.md, "Models as glTF"), which
   hands it every file through its own callback: cgltf's own file reads are never used. */
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
