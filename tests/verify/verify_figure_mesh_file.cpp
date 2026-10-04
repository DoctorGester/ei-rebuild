// Checks for CFigureMesh's file save / load (src/game/figure_mesh_file.cpp). Both touch files, so
// only ours runs; the load is checked for array sizes that match what it read.
#include "verify.h"

#include "../../src/game/figure_mesh.h"

namespace {
void* s_unused;
}

// 0059F710.
static void __fastcall FigureMeshSaveVerify(CFigureMesh* self, void*, void* file) {
    self->SaveToFile(file);
    ei::verify::Check("CFigureMesh::SaveToFile", true, true, "%d vertex packets", self->buffers_.vertexCount);
}
EI_VERIFY(0x0059F710, FigureMeshSaveVerify, 0, s_unused);

// 0059F8F0: the arrays' sizes are not negative.
static void __fastcall FigureMeshLoadVerify(CFigureMesh* self, void*, void* file) {
    self->LoadFromFile(file);
    bool ok = self->vertices_.size >= 0 && self->normals_.size >= 0 && self->corners_.size >= 0 &&
              self->indices_.size >= 0;
    ei::verify::Check("CFigureMesh::LoadFromFile", true, ok, "%d vertex packets", self->vertices_.size);
}
EI_VERIFY(0x0059F8F0, FigureMeshLoadVerify, 0, s_unused);
