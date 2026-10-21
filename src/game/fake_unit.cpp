// CFakeUnit (vtable 0073BFE8, class id 0x50, 0x580 bytes; derives from CUnit; declared as
// scene::FakeUnitScene, scene_loader.h): constructor, destructor, Destroy (slot 3), the scene load
// (slot 62) and the class id. It adds the skin at +0x4E0 (constructor 00508700, destructor
// 005087B0) and four CArrays: the figures (+0x530, CFigureMesh records, vtable 0073C108), the
// meshes (+0x544, SceneMesh, 0073C0FC), the bone links (+0x558, a CArray each, 0073C0F0) and the
// helpers (+0x56C, CMaterial, 0073C0E4). Every callee is reached through its original address.
// See docs/notes/objects.md ("Rewritten").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "figure_mesh.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "record_arrays.h"
#include "scene_loader.h"
#include "world.h"
#include "lib/mfc_slots.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace fakeunit {

namespace {

using scene::FakeUnitScene;

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

__declspec(naked) void __fastcall UnitConstruct(void*) EI_ORIGINAL(0x0052E110)
__declspec(naked) void __fastcall UnitDestroy(void*) EI_ORIGINAL(0x0052E590)

// The skin emptied and the unit's own Destroy (the start of both the destructor and slot 3).
void ReleaseParts(FakeUnitScene* self) {
    self->skin.Clear();
    self->skin.SetOwner(0);
    UnitDestroy(self);
}

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

// 004466D0: the unit, the skin, four empty arrays.
void* __fastcall FakeUnitConstruct(FakeUnitScene* self) {
    UnitConstruct(self);
    self->skin.Construct();
    self->figures.Construct(mfc::impl::FakeUnitPartArrayClass::Vtable());
    self->meshes.Construct(mfc::impl::FakeUnitSceneRecordArrayClass::Vtable());
    self->boneLinks.Construct(mfc::impl::FakeUnitBoneArraysClass::Vtable());
    self->helpers.Construct(mfc::impl::FakeUnitRecord9CArrayClass::Vtable());
    ei::SetVtable(self, FakeUnitScene::kVtableAddress);
    return self;
}

} // namespace fakeunit

// 004468F0: the scalar deleting destructor.
void* scene::FakeUnitScene::DeleteScalar(unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

namespace fakeunit {
EI_REPLACE_METHOD(0x004468F0, scene::FakeUnitScene::DeleteScalar);

// 00446B20 (slot 3): Destroy: as the destructor's start, then the figures destroyed (slot 0 with 0)
// and their array emptied.
void __fastcall FakeUnitDestroy(FakeUnitScene* self) {
    ReleaseParts(self);
    FigureArray& figures = Figures(self);
    if (figures.data) {
        for (int i = 0, n = figures.size; i < n; ++i) DestroyRecord(&figures.data[i]);
        gamecrt::operator_delete(figures.data);
        figures.data = nullptr;
    }
    figures.maxSize = 0;
    figures.size = 0;
}
EI_REPLACE(0x00446B20, FakeUnitDestroy);

// 00446BA0 (slot 62, RET 8): loads the scene; on success each figure gets a new object of class
// 0x45 from the world's side-specific manager (slot 21; its CreateObject, slot 23). Returns the
// loader's 0, or 1.
int __fastcall FakeUnitLoadScene(FakeUnitScene* self, void*, const char* file, const char* regFile) {
    if (!scene::LoadFakeUnitScene(file, regFile, self)) return 0;   // 00446C10 (ECX the unit as well, not read)
    for (int i = 0; i < Figures(self).size; ++i) {
        CBaseManager* manager = static_cast<CWorld*>(ManagerContextOf(self))->SpecificManager();
        CEngineObject* object = manager->CreateObject(0x45, -1);
        static_cast<CFigureMesh*>(&Figures(self).data[i])->SetDrawer(reinterpret_cast<FigureMeshDrawer*>(object));
    }
    return 1;
}
EI_REPLACE(0x00446BA0, FakeUnitLoadScene);

// 004468E0: the class id.
int __fastcall FakeUnitClassId(void*) { return 0x50; }
EI_REPLACE(0x004468E0, FakeUnitClassId);

} // namespace fakeunit
