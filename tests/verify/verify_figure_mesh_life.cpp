// A-W13: the figure meshes' lifetime (src/game/figure_mesh_life.cpp).
//
// Create and ConstructBase write plain fields only: the original runs on a scratch copy of the
// object's first 0x1C0 bytes (what the base owns), ours on the object, and the two are compared.
// The clears, destructor bodies, setters and the figure manager's two functions free memory or
// release objects, so they are ours only with the state they must leave checked.
// Reached whenever a figure is made or dropped: the interface's 3D models at the menus, every part
// of every unit and object figure of a new game (CreateFigureObject, ConstructBase, Create,
// SetDrawer, SetOwner), and leaving the game or the menu screen (the clears and destructors). The
// figure manager is constructed with each world (WorldCreateManagers).
#include "verify.h"

#include "../../src/game/figure_manager.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/link_figure.h"

#include <cstring>

namespace {
void* s_unused;

constexpr std::size_t kBaseSize = offsetof(CFigureMesh, vertices_);
constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;
constexpr std::uintptr_t kFigureManagerVtable = 0x0073FB88;

std::uintptr_t VtableOf(const void* object) { return *static_cast<const std::uintptr_t*>(object); }
int& RefCountOf(FigureMeshDrawer* drawer) { return reinterpret_cast<CEngineObject*>(drawer)->refCount_; }

template <typename T>
bool Empty(const lib::AlignedArray<T>& array) {
    return array.data == nullptr && array.size == 0 && array.capacity == 0;
}

bool BuffersClear(const CFigureMesh* mesh) {
    static const MeshBuffers kNone = {};
    return std::memcmp(&mesh->buffers_, &kNone, sizeof(kNone)) == 0;
}

// What Clear leaves.
bool Cleared(const CFigureMesh* mesh) {
    return mesh->name_.GetLength() == 0 && mesh->directory_.GetLength() == 0 && Empty(mesh->offsets_) &&
           Empty(mesh->colorsA_) && Empty(mesh->colorsB_) && BuffersClear(mesh) && mesh->unknown_158 == 0 &&
           mesh->meshType_ == 0 && mesh->drawer_ == nullptr && mesh->flags_ == 0;
}

int(__fastcall* s_create)(CFigureMesh*);
CFigureMesh*(__fastcall* s_constructBase)(CFigureMesh*);

// The first byte that differs in the base part, or -1.
int BaseDifference(const CFigureMesh* a, const unsigned char* b) {
    const auto* pa = reinterpret_cast<const unsigned char*>(a);
    for (std::size_t i = 0; i < kBaseSize; ++i)
        if (pa[i] != b[i]) return static_cast<int>(i);
    return -1;
}

// 0059F210: MOV EDX,ECX; XOR EAX,EAX; PUSH EDI (5 bytes).
int __fastcall CreateVerify(CFigureMesh* self) {
    alignas(16) unsigned char scratch[kBaseSize];
    std::memcpy(scratch, self, kBaseSize);
    int expected = s_create(reinterpret_cast<CFigureMesh*>(scratch));
    int actual = self->Create();
    int diff = BaseDifference(self, scratch);
    ei::verify::Check("CFigureMesh::Create", true, expected == actual && diff < 0, "%d vs %d, +0x%X", expected, actual,
                      diff);
    return expected;
}

// 005AABC0: PUSH -1; PUSH 0x7270F6 (7 bytes). The memory is fresh from operator new: both versions
// start from the same bytes.
CFigureMesh* __fastcall ConstructBaseVerify(CFigureMesh* self) {
    alignas(16) unsigned char scratch[kBaseSize];
    std::memcpy(scratch, self, kBaseSize);
    CFigureMesh* expected = s_constructBase(reinterpret_cast<CFigureMesh*>(scratch));
    CFigureMesh* actual = self->ConstructBase();
    int diff = BaseDifference(self, scratch);
    ei::verify::Check("CFigureMesh::ConstructBase", true,
                      expected == reinterpret_cast<CFigureMesh*>(scratch) && actual == self && diff < 0, "+0x%X", diff);
    return actual;
}

void __fastcall ClearVerify(CFigureMesh* self) {
    self->Clear();
    ei::verify::Check("CFigureMesh::Clear", true, Cleared(self), "mesh %p", self);
}

void __fastcall ClearMeshVerify(CFigureMesh* self) {
    self->ClearMesh();
    bool ok = Cleared(self) && Empty(self->vertices_) && Empty(self->normals_) && Empty(self->corners_) &&
              Empty(self->words_) && Empty(self->indices_) && Empty(self->texCoords_);
    ei::verify::Check("CFigureMesh::ClearMesh", true, ok, "mesh %p", self);
}

void __fastcall ClearSharedMeshVerify(CFigureMesh* self) {
    self->ClearSharedMesh();
    ei::verify::Check("CFigureMesh::ClearSharedMesh", true,
                      Cleared(self) && Empty(self->vertices_) && Empty(self->normals_), "mesh %p", self);
}

// What the destructor bodies leave: the engine object's vtable and no manager.
bool Destructed(const CFigureMesh* mesh) { return VtableOf(mesh) == kEngineObjectVtable && mesh->manager_ == nullptr; }

void __fastcall DestructBaseVerify(CFigureMesh* self) {
    self->DestructBase();
    ei::verify::Check("CFigureMesh::DestructBase", true, Destructed(self), "mesh %p", self);
}

void __fastcall DestructMeshVerify(CFigureMesh* self) {
    self->DestructMesh();
    ei::verify::Check("CFigureMesh::DestructMesh", true, Destructed(self), "mesh %p", self);
}

void __fastcall DestructSharedMeshVerify(CFigureMesh* self) {
    self->DestructSharedMesh();
    ei::verify::Check("CFigureMesh::DestructSharedMesh", true, Destructed(self), "mesh %p", self);
}

// The deleting destructors free the object when bit 0 is set: only the result is checked then.
CFigureMesh* __fastcall DeletingDestructMeshVerify(CFigureMesh* self, void*, unsigned flags) {
    CFigureMesh* result = self->DeletingDestructMesh(flags);
    ei::verify::Check("CFigureMesh::DeletingDestructMesh", true, result == self && ((flags & 1) || Destructed(self)),
                      "mesh %p flags %u", self, flags);
    return result;
}

CFigureMesh* __fastcall DeletingDestructSharedMeshVerify(CFigureMesh* self, void*, unsigned flags) {
    CFigureMesh* result = self->DeletingDestructSharedMesh(flags);
    ei::verify::Check("CFigureMesh::DeletingDestructSharedMesh", true,
                      result == self && ((flags & 1) || Destructed(self)), "mesh %p flags %u", self, flags);
    return result;
}

// The new drawer points back and holds one more reference (none more when it was the mesh's
// drawer already and survived its release).
void __fastcall SetDrawerVerify(CFigureMesh* self, void*, FigureMeshDrawer* drawer) {
    bool same = self->drawer_ == drawer;
    int before = drawer ? RefCountOf(drawer) : 0;
    self->SetDrawer(drawer);
    bool ok = self->drawer_ == drawer &&
              (!drawer || (drawer->mesh_ == self && RefCountOf(drawer) == before + (same ? 0 : 1)));
    ei::verify::Check("CFigureMesh::SetDrawer", true, ok, "mesh %p drawer %p", self, drawer);
}

void __fastcall SetOwnerVerify(CFigureMesh* self, void*, CObject3D* owner) {
    self->SetOwner(owner);
    ei::verify::Check("CFigureMesh::SetOwner", true, self->owner_ == owner, "mesh %p", self);
}

// 005A2480 (A-W14; MOV EAX,[ECX+8]; OR AL,0x20: 5 bytes), the base's slot 19. Differential on the
// flags: the original first, the flags put back, then ours. Reached when a class 0x44 figure
// (the base class, what the factory makes for that id) is loaded through its slot 19; the
// figures of the scenarios are classes 0x41-0x43, so none is known to reach it.
void(__fastcall* s_loadNothing)(CFigureMesh*, void*, char**, float, float, float);
void __fastcall LoadNothingVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    std::uint32_t before = self->flags_;
    s_loadNothing(self, nullptr, cursor, s, t, u);
    std::uint32_t expected = self->flags_;
    self->flags_ = before;
    self->LoadNothing(cursor, s, t, u);
    ei::verify::Check("CFigureMesh::LoadNothing", expected, self->flags_, "mesh %p", self);
}

CFigureManager* __fastcall ManagerConstructVerify(CFigureManager* self) {
    CFigureManager* result = self->Construct();
    bool ok = result == self && VtableOf(self) == kFigureManagerVtable && self->models_.Size() == 0 &&
              self->models_.BucketCount() == stlp::NextPrime(100) && self->refCount_ == 0 && self->owner_ == nullptr;
    ei::verify::Check("CFigureManager::Construct", true, ok, "%p", self);
    return result;
}

// A new figure has a drawer that points back at it; other classes come back as the base makes them.
CEngineObject* __fastcall CreateFigureObjectVerify(CFigureManager* self, void*, int type, int id) {
    CEngineObject* object = self->CreateFigureObject(type, id);
    bool ok = true;
    if (object != nullptr && type >= 0x41 && type <= 0x44) {
        auto* mesh = reinterpret_cast<CFigureMesh*>(object);
        ok = mesh->drawer_ != nullptr && mesh->drawer_->mesh_ == mesh;
    }
    ei::verify::Check("CFigureManager::CreateFigureObject", true, ok, "type 0x%X id %d", type, id);
    return object;
}
} // namespace

EI_VERIFY(0x0059F210, CreateVerify, 5, s_create);
EI_VERIFY(0x005AABC0, ConstructBaseVerify, 7, s_constructBase);
EI_VERIFY(0x0059F2B0, ClearVerify, 0, s_unused);
EI_VERIFY(0x005A6C80, ClearMeshVerify, 0, s_unused);
EI_VERIFY(0x005A6E10, ClearSharedMeshVerify, 0, s_unused);
EI_VERIFY(0x0059F030, DestructBaseVerify, 0, s_unused);
EI_VERIFY(0x005AAD00, DestructMeshVerify, 0, s_unused);
EI_VERIFY(0x005AADC0, DestructSharedMeshVerify, 0, s_unused);
EI_VERIFY(0x005AACE0, DeletingDestructMeshVerify, 0, s_unused);
EI_VERIFY(0x005AADA0, DeletingDestructSharedMeshVerify, 0, s_unused);
EI_VERIFY(0x0059F390, SetDrawerVerify, 0, s_unused);
EI_VERIFY(0x0059F3D0, SetOwnerVerify, 0, s_unused);
EI_VERIFY(0x005A2480, LoadNothingVerify, 5, s_loadNothing);
EI_VERIFY(0x005AA070, ManagerConstructVerify, 0, s_unused);
EI_VERIFY(0x005AAE00, CreateFigureObjectVerify, 0, s_unused);
