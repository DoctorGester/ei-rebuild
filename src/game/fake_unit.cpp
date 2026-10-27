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

// The figure records and the helpers are engine objects (vtable and count first; the classes
// declare no virtuals): slot 0, the deleting destructor, with 0.
void DestroyRecord(CEngineObject* record) { record->~CEngineObject(); }

// The array's vtable back, each element destroyed (`destroy`; the destructor's loops are the
// instantiations' inlined DestructElements: each element's destructor through its vtable), the
// block freed; the fields are left as they are.
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
    figures.SetSize(0, -1);
    scene::FigurePartArray& figureRecords = figures;
    for (int i = 0; i < figureRecords.size; ++i) --figureRecords.data[i].refCount_;
    DestructArray(helpers, recordarrays::FakeUnitRecord9CArrayClass::Vtable(), [](CMaterial& helper) { DestroyRecord(&helper); });
    DestructArray(boneLinks, mfc::impl::FakeUnitBoneArraysClass::Vtable(),
                  [](scene::BoneLinkRecords& links) { mfc::impl::DeleteThroughObject(&links); });
    DestructArray(meshes, recordarrays::FakeUnitSceneRecordArrayClass::Vtable(), [](scene::SceneMesh& mesh) { DestroyRecord(&mesh); });
    DestructArray(figureRecords, recordarrays::FakeUnitPartArrayClass::Vtable(), [](CFigureMesh& record) { DestroyRecord(&record); });
}

void FakeUnitScene::Destroy() {
    using namespace fakeunit;
    ReleaseParts();
    scene::FigurePartArray& figureRecords = figures;
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
    scene::FigurePartArray& figureRecords = figures;
    for (int i = 0; i < figureRecords.size; ++i) {
        CBaseManager* manager = WorldContext()->SpecificManager();
        CEngineObject* object = manager->CreateObject(0x45, -1);
        figureRecords.data[i].SetDrawer(static_cast<FigureMeshDrawer*>(object));
    }
    return 1;
}

EI_REWRITTEN_SPECIAL(0x004466D0, FakeUnitScene::FakeUnitScene);
EI_REWRITTEN_SPECIAL(0x00446910, FakeUnitScene::~FakeUnitScene);
EI_REWRITTEN_SPECIAL(0x004468F0, FakeUnitScene::~FakeUnitScene);   // deleting

namespace fakeunit {

// The slots of game.exe's 0073BFE8 (retired) are the compiled table's; nothing reaches their
// addresses (tools/registration_readers.py).
EI_REWRITTEN(0x00446B20, FakeUnitScene::Destroy);     // slot 3
EI_REWRITTEN(0x00446BA0, FakeUnitScene::LoadScene);   // slot 62, RET 8
EI_REWRITTEN(0x004468E0, FakeUnitScene::GetClassId);  // slot 12: the class id

} // namespace fakeunit
