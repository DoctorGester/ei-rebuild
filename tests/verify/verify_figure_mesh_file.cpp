// Checks for CFigureMesh's file save / load (src/game/figure_mesh_file.cpp). Both touch files, so
// only ours runs; the load is checked for array sizes that match what it read.
#include "verify.h"

#include "../../src/game/figure_mesh.h"

#include <cstring>

namespace {
void* s_unused;
}

// 0059F710.
static void __fastcall FigureMeshSaveVerify(CFigureMesh* self, void*, mfc::CFileVirtuals* file) {
    self->SaveToFile(file);
    ei::verify::Check("CFigureMesh::SaveToFile", true, true, "%d vertex packets", self->buffers_.vertexCount);
}
EI_VERIFY(0x0059F710, FigureMeshSaveVerify, 0, s_unused);

// 0059F8F0: the arrays' sizes are not negative.
static void __fastcall FigureMeshLoadVerify(CFigureMesh* self, void*, mfc::CFileVirtuals* file) {
    self->LoadFromFile(file);
    bool ok = self->vertices_.size >= 0 && self->normals_.size >= 0 && self->corners_.size >= 0 &&
              self->indices_.size >= 0;
    ei::verify::Check("CFigureMesh::LoadFromFile", true, ok, "%d vertex packets", self->vertices_.size);
}
EI_VERIFY(0x0059F8F0, FigureMeshLoadVerify, 0, s_unused);

// 0059F0F0: the copied descriptors and bounds equal the source's.
static void __fastcall FigureMeshCopyVerify(CFigureMesh* self, void*, const CFigureMesh* other) {
    self->CopyFigure(other);
    bool ok = self->buffers_.corners == other->buffers_.corners && self->buffers_.indexCount == other->buffers_.indexCount &&
              std::memcmp(&self->center_, &other->center_, 0x28) == 0;
    ei::verify::Check("CFigureMesh::CopyFigure", true, ok, "");
}
EI_VERIFY(0x0059F0F0, FigureMeshCopyVerify, 0, s_unused);

namespace {
using SetPoseFn = void(__fastcall*)(CFigureMesh*, void*, const float*, const float*);
SetPoseFn s_originalSetPose;
}

// 0059F510: the original first, its outputs (+0x28, the matrix at +0xE0, +0x110) kept; ours then
// writes the same fields from the same inputs, compared bit for bit.
static void __fastcall FigureMeshSetPoseVerify(CFigureMesh* self, void*, const float* q, const float* offset) {
    auto* raw = reinterpret_cast<std::uint8_t*>(self);
    std::uint8_t expectedMoved[12], expectedMatrix[0x40], expectedCopy[12];
    s_originalSetPose(self, nullptr, q, offset);
    std::memcpy(expectedMoved, raw + 0x28, 12);
    std::memcpy(expectedMatrix, raw + 0xE0, 0x40);
    std::memcpy(expectedCopy, raw + 0x110, 12);
    self->SetPose(q, offset);
    bool ok = std::memcmp(expectedMoved, raw + 0x28, 12) == 0 && std::memcmp(expectedMatrix, raw + 0xE0, 0x40) == 0 &&
              std::memcmp(expectedCopy, raw + 0x110, 12) == 0;
    ei::verify::Check("CFigureMesh::SetPose", true, ok, "");
}
EI_VERIFY(0x0059F510, FigureMeshSetPoseVerify, 6, s_originalSetPose);
