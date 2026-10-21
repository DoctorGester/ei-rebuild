// CFakeUnit (class id 0x50, 0x580 bytes; derives from CUnit; declared as scene::FakeUnitScene,
// scene_loader.h): constructor, destructor, Destroy (slot 3), the scene load (slot 62) and the class
// id. It adds the skin at +0x4E0 (constructor 00508700, destructor 005087B0) and four CArrays: the
// figures (+0x530, CFigureMesh records, vtable 0073C108), the meshes (+0x544, SceneMesh, 0073C0FC),
// the bone links (+0x558, a CArray each, 0073C0F0) and the helpers (+0x56C, CMaterial, 0073C0E4).
// A real virtual class (C19: game.exe's 0073BFE8 is retired). See docs/notes/objects.md
// ("Rewritten").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "figure_mesh.h"
#include "link_figure.h"   // FigureMeshDrawer
#include "originals_engine.h"
#include "originals_units.h"
#include "record_arrays.h"
#include "scene_loader.h"
#include "world.h"
#include "lib/mfc_slots.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

using scene::FakeUnitScene;

namespace fakeunit {

namespace {

constexpr int kFakeUnitClassId = 0x50;

// The unit's arrays with their element types (the destructor's loops are the instantiations'
// inlined DestructElements: each element's destructor through its vtable).
using FigureArray = mfc::CArray<CFigureMesh, mfc::RawElements<CFigureMesh>>;
using BoneLinkArray = mfc::CArray<scene::VtArray, mfc::RawElements<scene::VtArray>>;
FigureArray& Figures(FakeUnitScene* self) { return scene::Typed<FigureArray>(self->figures); }
BoneLinkArray& BoneLinks(FakeUnitScene* self) { return scene::Typed<BoneLinkArray>(self->boneLinks); }
scene::MeshArray& Meshes(FakeUnitScene* self) { return scene::Typed<scene::MeshArray>(self->meshes); }
scene::HelperArray& Helpers(FakeUnitScene* self) { return scene::Typed<scene::HelperArray>(self->helpers); }

// The figure records and the helpers are engine objects (vtable and count first; the classes
// declare no virtuals): slot 0, the deleting destructor, with 0.
void DestroyRecord(void* record) { static_cast<CEngineObject*>(record)->~CEngineObject(); }

// The array's vtable back, each element destroyed (`destroy`), the block freed; the fields are
// left as they are.
template <typename Array, typename Destroy>
void DestructArray(Array& a, const void* vtable, Destroy destroy) {
    ei::SetVtable(&a, vtable);
    if (!a.data) return;
    for (int i = 0, n = a.size; i < n; ++i) destroy(a.data[i]);
    gamecrt::operator_delete(a.data);
}

} // namespace

} // namespace fakeunit

FakeUnitScene::FakeUnitScene() {
    figures.Construct(recordarrays::FakeUnitPartArrayClass::Vtable());
    meshes.Construct(recordarrays::FakeUnitSceneRecordArrayClass::Vtable());
    boneLinks.Construct(mfc::impl::FakeUnitBoneArraysClass::Vtable());
    helpers.Construct(recordarrays::FakeUnitRecord9CArrayClass::Vtable());
}

void FakeUnitScene::ReleaseParts() {
    skin.Clear();
    skin.SetOwner(0);
    DestroyUnit();
}

// After the figures' SetSize(0, -1) the original walks the (now empty) figure array dropping each
// figure's count; kept as written.
FakeUnitScene::~FakeUnitScene() {
    using namespace fakeunit;
    ReleaseParts();
    scene::Typed<recordarrays::FigureParts::Array>(figures).SetSize(0, -1);   // 0044FCD0
    FigureArray& figureRecords = Figures(this);
    for (int i = 0; i < figureRecords.size; ++i) --figureRecords.data[i].refCount_;
    DestructArray(Helpers(this), recordarrays::FakeUnitRecord9CArrayClass::Vtable(), [](CMaterial& helper) { DestroyRecord(&helper); });
    DestructArray(BoneLinks(this), mfc::impl::FakeUnitBoneArraysClass::Vtable(), [](scene::VtArray& links) {
        reinterpret_cast<mfc::CObjectVirtuals*>(&links)->DeletingDestructor(0);
    });
    DestructArray(Meshes(this), recordarrays::FakeUnitSceneRecordArrayClass::Vtable(), [](scene::SceneMesh& mesh) { DestroyRecord(&mesh); });
    DestructArray(figureRecords, recordarrays::FakeUnitPartArrayClass::Vtable(), [](CFigureMesh& record) { DestroyRecord(&record); });
}

void FakeUnitScene::Destroy() {
    using namespace fakeunit;
    ReleaseParts();
    FigureArray& figureRecords = Figures(this);
    if (figureRecords.data) {
        for (int i = 0, n = figureRecords.size; i < n; ++i) DestroyRecord(&figureRecords.data[i]);
        gamecrt::operator_delete(figureRecords.data);
        figureRecords.data = nullptr;
    }
    figureRecords.maxSize = 0;
    figureRecords.size = 0;
}

int FakeUnitScene::GetClassId() { return fakeunit::kFakeUnitClassId; }

int FakeUnitScene::LoadScene(const char* file, const char* regFile) {
    if (!scene::LoadFakeUnitScene(file, regFile, this)) return 0;   // 00446C10 (ECX the unit as well, not read)
    fakeunit::FigureArray& figureRecords = fakeunit::Figures(this);
    for (int i = 0; i < figureRecords.size; ++i) {
        CBaseManager* manager = static_cast<CWorld*>(ManagerContextOf(this))->SpecificManager();
        CEngineObject* object = manager->CreateObject(0x45, -1);
        static_cast<CFigureMesh*>(&figureRecords.data[i])->SetDrawer(static_cast<FigureMeshDrawer*>(object));
    }
    return 1;
}

void* FakeUnitScene::DeleteScalar(unsigned flags) {
    this->FakeUnitScene::~FakeUnitScene();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// The registrations (C20 retires them).
namespace fakeunit {

// 004466D0: the constructor, in place.
void* __fastcall FakeUnitConstruct(FakeUnitScene* self) { return new (self) FakeUnitScene(); }
EI_REPLACE(0x004466D0, FakeUnitConstruct);

// 00446910: the destructor.
void __fastcall FakeUnitDestruct(FakeUnitScene* self) { self->FakeUnitScene::~FakeUnitScene(); }
EI_REPLACE(0x00446910, FakeUnitDestruct);

EI_REPLACE_METHOD(0x004468F0, FakeUnitScene::DeleteScalar);

// 00446B20 (slot 3).
void __fastcall FakeUnitDestroy(FakeUnitScene* self) { self->FakeUnitScene::Destroy(); }
EI_REPLACE(0x00446B20, FakeUnitDestroy);

// 00446BA0 (slot 62, RET 8).
int __fastcall FakeUnitLoadScene(FakeUnitScene* self, void*, const char* file, const char* regFile) {
    return self->FakeUnitScene::LoadScene(file, regFile);
}
EI_REPLACE(0x00446BA0, FakeUnitLoadScene);

// 004468E0 (slot 12): the class id.
int __fastcall FakeUnitClassId(FakeUnitScene* self) { return self->FakeUnitScene::GetClassId(); }
EI_REPLACE(0x004468E0, FakeUnitClassId);

} // namespace fakeunit
