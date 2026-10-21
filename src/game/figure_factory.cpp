// 005AA8E0 (virtual, slot of the figure object factory 0073FBC0; thiscall, RET 4, `this` unused):
// creates a scene object of a class id. 0x41-0x43 are the figure part meshes built on the common
// base (005AABC0): 0x41 with two empty arrays (vtable 0073FBF0), 0x42 with three more and three
// small members (0073C1C4), 0x43 with six constructed members (0073FC48); 0x44 (004D9110) and
// 0x300B-0x300D (0050A5D0, 0050AF40, 0050C5F0) have their own constructors. The new object's
// type_ (+0x10) gets the class id; an unknown id or a failed allocation returns null. See
// docs/notes/models.md.
#include "engine_object.h"   // CEngineObject
#include "figure_mesh.h"     // CFigureMesh
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "sky_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "scene_loader.h"

#include <cstdint>

namespace {

// The member constructors (every field zero, returns the member; AlignedArray::Construct, sky_object.cpp):
// 0056FD00 for the vertex and normal arrays, 004D9E90 / 004D9E80 / 004D9EA0 for the words, indices and
// texture coordinates; 005AD870 (figure_slots.cpp ClearArray) for the corners.

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
    if (object) static_cast<CFigureMesh*>(object)->ConstructBase();
    return static_cast<CFigureMesh*>(object);
}

void SetVtable(CFigureMesh* mesh, std::uintptr_t address) { ei::SetVtable(mesh, ei::GamePointer<const void>(address)); }

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
        o->words_.Construct();
        o->indices_.Construct();
        o->texCoords_.Construct();
        SetVtable(o, CFigureMesh::kPlainMeshVtableAddress);
        return o;
    }
    case 0x43: {
        CFigureMesh* o = NewPart(0x284);
        if (!o) return nullptr;
        o->vertices_.Construct();
        o->normals_.Construct();
        figure::ClearArray(reinterpret_cast<std::uint32_t*>(&o->corners_));   // 005AD870
        o->words_.Construct();
        o->indices_.Construct();
        o->texCoords_.Construct();
        SetVtable(o, CFigureMesh::kMeshVtableAddress);
        return o;
    }
    case 0x44: {
        void* o = gamecrt::operator_new(0x230);
        return o ? static_cast<CSkyObject*>(o)->Construct() : nullptr;
    }
    case 0x300B: {
        void* o = gamecrt::operator_new(0x78);
        return o ? static_cast<AnimationTrack*>(o)->Construct() : nullptr;
    }
    case 0x300C: {
        void* o = gamecrt::operator_new(0xA0);
        return o ? static_cast<FigureBone*>(o)->Construct() : nullptr;
    }
    case 0x300D: {
        void* o = gamecrt::operator_new(0x78);
        return o ? static_cast<CFigureAnimationState*>(o)->Construct() : nullptr;
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

