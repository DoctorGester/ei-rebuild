// CFakeUnit (class id 0x50; derives from CUnit; declared as scene::FakeUnitScene, scene_loader.h): the skin and
// four CArrays (figures, meshes, bone links, helpers) over a unit. See docs/notes/objects.md ("Rewritten").
#include "lib/crt.h"
#include "lib/mfc.h"
#include "figure_mesh.h"
#include "link_figure.h"
#include "record_arrays.h"
#include "scene_loader.h"
#include "world.h"
#include "lib/mfc_slots.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName

using scene::FakeUnitScene;

namespace fakeunit {

namespace {

constexpr int kFakeUnitClassId = 0x50;

// The figure records and the helpers are engine objects: destroyed through their virtual destructor.
void DestroyRecord(CEngineObject* record) { record->~CEngineObject(); }

// The array's vtable back, each element destroyed (`destroy`), the block freed; the fields are left as they are.
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

// After the figures' SetSize(0, -1) the game walks the (now empty) figure array dropping each
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
    if (!scene::LoadFakeUnitScene(file, regFile, this)) return 0;
    scene::FigurePartArray& figureRecords = figures;
    for (int i = 0; i < figureRecords.size; ++i) {
        CBaseManager* manager = WorldContext()->SpecificManager();
        figureRecords.data[i].SetDrawer(manager->CreateObjectAs<FigureMeshDrawer>(0x45, -1));
    }
    return 1;
}
