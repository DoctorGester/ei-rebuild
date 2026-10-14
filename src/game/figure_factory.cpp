// 005AA8E0 (virtual, slot of the figure object factory 0073FBC0; thiscall, RET 4, `this` unused):
// creates a scene object of a class id. 0x41-0x43 are the figure part meshes built on the common
// base (005AABC0): 0x41 with two empty arrays (vtable 0073FBF0), 0x42 with three more and three
// small members (0073C1C4), 0x43 with six constructed members (0073FC48); 0x44 (004D9110) and
// 0x300B-0x300D (0050A5D0, 0050AF40, 0050C5F0) have their own constructors. The new object's
// type_ (+0x10) gets the class id; an unknown id or a failed allocation returns null. See
// docs/notes/models.md.
#include "engine_object.h"   // CEngineObject
#include "figure_mesh.h"     // CFigureMesh

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstdint>

namespace {

__declspec(naked) CFigureMesh* __fastcall ConstructPartBase(void*) EI_ORIGINAL(0x005AABC0)
// The member constructors (every field zero, returns the member): 0056FD00 for the vertex and
// normal arrays, 005AD870 (figure_slots.cpp ClearArray) for the corners, 004D9E90 / 004D9E80 /
// 004D9EA0 (sky_object.cpp, AlignedArray::Construct) for the words, indices and texture coordinates.
__declspec(naked) void* __fastcall ConstructPackets(void*) EI_ORIGINAL(0x0056FD00)
__declspec(naked) void* __fastcall ConstructCorners(lib::AlignedArray<MeshCorner>*) EI_ORIGINAL(0x005AD870)
__declspec(naked) void* __fastcall ConstructWords(lib::AlignedArray<std::uint32_t>*) EI_ORIGINAL(0x004D9E90)
__declspec(naked) void* __fastcall ConstructIndices(lib::AlignedArray<std::uint16_t>*) EI_ORIGINAL(0x004D9E80)
__declspec(naked) void* __fastcall ConstructTexCoords(lib::AlignedArray<TexCoord>*) EI_ORIGINAL(0x004D9EA0)
__declspec(naked) void* __fastcall Construct44(void*) EI_ORIGINAL(0x004D9110)
__declspec(naked) void* __fastcall Construct300B(void*) EI_ORIGINAL(0x0050A5D0)
__declspec(naked) void* __fastcall Construct300C(void*) EI_ORIGINAL(0x0050AF40)
__declspec(naked) void* __fastcall Construct300D(void*) EI_ORIGINAL(0x0050C5F0)

// The inlined array constructor: the four fields zeroed in order.
template <typename T>
void ZeroArray(lib::AlignedArray<T>& array) {
    array.data = nullptr;
    array.size = 0;
    array.capacity = 0;
    array.growBy = 0;
}

CFigureMesh* NewPart(std::size_t size) {
    void* object = gamecrt::operator_new(size);
    if (object) ConstructPartBase(object);
    return static_cast<CFigureMesh*>(object);
}

void SetVtable(CFigureMesh* mesh, std::uintptr_t address) { mesh->vtable_ = reinterpret_cast<void*>(address); }

void* Create(int classId) {
    switch (classId) {
    case 0x41: {
        CFigureMesh* o = NewPart(0x1E0);
        if (!o) return nullptr;
        ZeroArray(o->vertices_);
        ZeroArray(o->normals_);
        SetVtable(o, CFigureMesh::kSharedMeshVtableAddress);
        return o;
    }
    case 0x42: {
        CFigureMesh* o = NewPart(0x220);
        if (!o) return nullptr;
        ZeroArray(o->vertices_);
        ZeroArray(o->normals_);
        ZeroArray(o->corners_);
        ConstructWords(&o->words_);
        ConstructIndices(&o->indices_);
        ConstructTexCoords(&o->texCoords_);
        SetVtable(o, CFigureMesh::kPlainMeshVtableAddress);
        return o;
    }
    case 0x43: {
        CFigureMesh* o = NewPart(0x284);
        if (!o) return nullptr;
        ConstructPackets(&o->vertices_);
        ConstructPackets(&o->normals_);
        ConstructCorners(&o->corners_);
        ConstructWords(&o->words_);
        ConstructIndices(&o->indices_);
        ConstructTexCoords(&o->texCoords_);
        SetVtable(o, CFigureMesh::kMeshVtableAddress);
        return o;
    }
    case 0x44: {
        void* o = gamecrt::operator_new(0x230);
        return o ? Construct44(o) : nullptr;
    }
    case 0x300B: {
        void* o = gamecrt::operator_new(0x78);
        return o ? Construct300B(o) : nullptr;
    }
    case 0x300C: {
        void* o = gamecrt::operator_new(0xA0);
        return o ? Construct300C(o) : nullptr;
    }
    case 0x300D: {
        void* o = gamecrt::operator_new(0x78);
        return o ? Construct300D(o) : nullptr;
    }
    default:
        return nullptr;
    }
}

} // namespace

void* __fastcall CreateFigureObject(void*, void*, int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<CEngineObject*>(object)->type_ = classId;
    return object;
}

EI_REPLACE(0x005AA8E0, CreateFigureObject);
