// Checks for the figure object factory (src/game/figure_factory.cpp). Ours only (it allocates):
// a known class id gives an object carrying the id at +0x10 (and, for the part meshes 0x41-0x43,
// their vtable); an unknown one gives null.
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/figure_mesh.h"

#include <cstdint>

void* __fastcall CreateFigureObject(void*, void*, int classId);

namespace {
void* s_unused;

std::uint32_t ExpectedVtable(int classId) {
    switch (classId) {
    case 0x41: return CFigureMesh::kSharedMeshVtableAddress;
    case 0x42: return CFigureMesh::kPlainMeshVtableAddress;
    case 0x43: return CFigureMesh::kMeshVtableAddress;
    default: return 0;
    }
}
bool Known(int classId) { return (classId >= 0x41 && classId <= 0x44) || (classId >= 0x300B && classId <= 0x300D); }
} // namespace

// 005AA8E0.
static void* __fastcall CreateFigureObjectVerify(void* self, void*, int classId) {
    void* object = CreateFigureObject(self, nullptr, classId);
    bool ok;
    if (!Known(classId)) {
        ok = object == nullptr;
    } else {
        ok = object && static_cast<CEngineObject*>(object)->type_ == classId &&
             (!ExpectedVtable(classId) ||
              reinterpret_cast<std::uintptr_t>(static_cast<CFigureMesh*>(object)->vtable_) == ExpectedVtable(classId));
    }
    ei::verify::Check("CreateFigureObject", true, ok, "class %x -> %p", classId, object);
    return object;
}
EI_VERIFY(0x005AA8E0, CreateFigureObjectVerify, 0, s_unused);
