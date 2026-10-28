// CWorldClient's draw path: Render and DrawPass, with Rebirth's LandscapeDrawRadius and ObjectsDrawRadius
// behind their flags. See world.h and docs/notes/world.md. Both talk to Direct3D 7 through the renderer
// (TheRenderDevice(): d3dDevice_ the IDirect3DDevice7, lastLoadResult_ the last state block result) and to
// the scene pipeline's matrix stacks (ScenePipeline()) with the same calls in the same order as the original.
#include "ground_print.h"
#include "lightning.h"
#include "options.h"
#include "renderer_lines.h"
#include "net_session.h"
#include "sky_object.h"
#include "spell.h"   // CLight
#include "ui_flying_hit_points.h"
#include "view_matrices.h"
#include "world.h"
#include "figure_manager.h"
#include "world_data.h"

#include "camera.h"
#include "effects.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "model.h"
#include "terrain.h"
#include "texture.h"
#include "device.h"
#include "lib/vecmath.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "render/render.h"

#include <iterator>
#include <list>
#include <vector>
#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include "ui.h"
#include "world_deleting_destructors.h"
#include "world_effect.h"
#include "world_client_shadow_casters.h"
#include "lib/stlp_list_refs.h"
#include "game_data.h"
#include "game_singletons.h"
#include "settings_file.h"
#include "system_text.h"

// windows.h renames CEngineObject's GetClassName.
#undef GetClassName

namespace {

// IDirect3DDevice7 (the renderer's d3dDevice_): SetRenderState, ApplyStateBlock (its HRESULT goes to
// the renderer's lastLoadResult_).
void SetRenderState(CTextureDevice* renderer, int state, std::uint32_t value) {
    device::SetRenderState(device::D3D(renderer), static_cast<D3DRENDERSTATETYPE>(state), value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
void ApplyStateBlock(CTextureDevice* renderer, const std::uint32_t& handle) {
    renderer->lastLoadResult_ = device::ApplyStateBlock(device::D3D(renderer), handle);
    render::state::Block(handle);
}

// The pipeline's matrix stacks: 1 and 2 pushed by PushSceneMatrixOriginal, 5 the world draw's, 9 the views.
constexpr int kStackScene1 = 1;
constexpr int kStackScene2 = 2;
constexpr int kStackWorld = 5;
constexpr int kStackView = 9;

// worlddata::SceneCamera: the scene stack's top when the world's draw pushed it, while that draw runs.
Matrix4 g_sceneCamera;
bool g_haveSceneCamera = false;

void PushMatrix(MatrixStack& stack, const float* matrix) {
    int& depth = stack.count;
    Matrix4* matrices = stack.matrices;
    if (depth == 0) {
        depth = 1;
        std::memcpy(matrices, matrix, sizeof(Matrix4));
    } else {
        int d = ++depth;
        matrices[d - 1].Multiply(matrices[d - 2].m, matrix);
    }
}
void PopMatrix(MatrixStack& stack) {
    int& depth = stack.count;
    if (depth) --depth;
}

// A starter setting (HKCU\Software\Gipat.Ru\EI_Starter\Settings, a DWORD, 100 when absent) as addon turns it
// into the float it patches in: (float)(unsigned)value, read once (addon reads its settings when it loads).
float StarterDrawRadius(const char* name) {
    DWORD value = 100;
    DWORD data = 0, size = sizeof(data), type = 0;
    if (settings_file::ReadValue("Software\\Gipat.Ru\\EI_Starter\\Settings", name, &type, &data, &size) == 0 && type == REG_DWORD)
        value = data;
    float radius = static_cast<float>(static_cast<double>(value));
    return radius;
}

float LandscapeDrawRadius() {
    static const float setting = StarterDrawRadius("LandscapeDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::LandscapeDrawRadius) ? setting : 100.0f;
}

void VectorClear(std::vector<CMapObject*>* v) { v->clear(); }

// The object margin around the camera when collecting what to draw (100.0).
float ObjectsDrawMargin() { return 100.0f /*0073BAB0*/; }
float ObjectsDrawRadius() {
    static const float setting = StarterDrawRadius("ObjectsDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::ObjectsDrawRadius) ? setting : ObjectsDrawMargin();
}
float MapEdgeMargin() { return 1.0f /*0073B84C*/; }
// The length of the shadows' projection along the camera's Z axis.
float ShadowLength() { return (-0.02f /*0073F750*/); }
// The light effect settings (the blood print, footprint and scorch mark lists) and the game mode (1
// single player).
int LightEffectsSetting(const int& setting) { return setting; }
int LightEffects1B8() { return LightEffectsSetting(options::TheOptions()->enableBloodprints_); }
int LightEffects1BC() { return LightEffectsSetting(options::TheOptions()->enableFootprints_); }
int LightEffects1C0() { return LightEffectsSetting(options::TheOptions()->enableFireprints_); }
int GameMode() { return options::TheOptions()->selectionType_; }
const char* MarkerPartName() { return "hp"; }

float AddRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }
float SubRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
// a < b, or unordered (FCOMP's C0 is set then too).
bool Less(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b;
}
float SelectLess(float a, float b) { return Less(a, b) ? a : b; }
// t when 0 < t or unordered, else 0.
float ClampToZero(float t) { return Less(0.0f, t) ? t : 0.0f; }
// A float copied through the FPU (FLD / FSTP) in the original; here the value as it is.
float Reload(float v) { return v; }
int Ftol(float v) { return static_cast<int>(gamecrt::Ftol(v)); }
// The client world's time: (float)(int)tick + the tick's fraction, rounded once (FILD, FADD, FSTP).
float AnimationTime() {
    const CWorldClient* world = TheClientWorld();
    return static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

// floor(log2(n)) (n >= 1).
int Log2(int n) {
    int depth = 0;
    while (n != 1) {
        n >>= 1;
        ++depth;
    }
    return depth;
}

// Sorts one draw list with UnitOrderLess. (The comparator is empty; nothing reads the renderer the
// original pushes for it.)
void SortByModel(FrameList list) {
    std::vector<CMapObject*>& v = FrameObjects(list);
    CMapObject** first = v.data();
    CMapObject** last = v.data() + v.size();
    if (first == last) return;
    IntroSortByModel(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, 0);
    FinalSortByModel(first, last, 0);
}

template <typename Draw>
void DrawEach(FrameList list, Draw draw) {
    for (CMapObject* object : FrameObjects(list)) draw(object);
}
// The drawn objects' three figure draw virtuals with the scene context; their results are ignored. The
// new renderer learns which object the meshes of a figure draw belong to (its shadow map's casters).
constexpr auto kDraw = [](CMapObjectVirtuals* object) {
    render::BeginCaster(object, false);
    object->DrawFigure(TheSceneRenderer());
    render::EndCaster();
};
constexpr auto kDrawUnit = [](CMapObjectVirtuals* object) {
    render::BeginCaster(object, true);
    object->DrawCollectDirty(TheSceneRenderer());
    render::EndCaster();
};
constexpr auto kDrawShadow = [](CMapObjectVirtuals* object) { object->DrawFigureShadow(TheSceneRenderer()); };

// Empties the deferred unit list: each node's counted pointer released, the node freed.
void EmptyDeferred() {
    using Node = stlp::list<CMapObject*>::Node;
    stlp::list<CMapObject*>* deferred = FrameDeferredUnits();
    Node* head = deferred->head;
    Node* node = head->next;
    while (node != head) {
        Node* current = node;
        node = node->next;
        if (current->data) current->data->Release();
        stlp::deallocate(current, sizeof(Node));
        head = deferred->head;
    }
    head->next = head;
    deferred->head->prev = deferred->head;
}

// Calls the ground print draw on the value of every node of an effect list (the ground print lists).
template <typename T>
void DrawLightEffects(stlp::list<T>& list) {
    typename stlp::list<T>::Node* head = list.head;
    for (typename stlp::list<T>::Node* node = head->next; node != head; node = node->next) {
        GroundPrint* print = &node->data;
        print->Draw();
    }
}
template <typename T>
bool ListEmpty(const stlp::list<T>& list) { return list.head->next == list.head; }

} // namespace

const Matrix4* worlddata::SceneCamera() { return g_haveSceneCamera ? &g_sceneCamera : nullptr; }

int __fastcall QueueScreenEntry(CMapObject* object, std::list<ScreenEntry>* queued) {
    if (!(object->flags_ & 0x20) || (object->flags_ & 0x40)) return 0;
    const CObject3D* figure = object;
    ScreenEntry entry;
    entry.classId = object->GetClassId();
    entry.objectId = static_cast<unsigned>(object->id_);
    entry.rect = figure->dirtyRect;
    queued->insert(queued->end(), entry);
    return 1;
}

int CWorldClient::Render() {
    // The world view for the new renderer's smooth option (multisampled, models sampled
    // anisotropically); the interface draws after it.
    render::BeginWorld();
    CTextureDevice* renderer = TheRenderDevice();
    CTransformPipeline* device = ScenePipeline();
    UpdateLighting();
    // The sun the new renderer's shadow map is drawn from: the terrain's (Terrain::SetSun), as this
    // frame's lighting turned it; the GPU lighting's sun, ambient and point lights.
    if (terrain_ && terrain_->sun) {
        const Vector3& d = terrain_->sun->direction_;
        const float direction[3] = {d.x, d.y, d.z};
        render::ShadowSun(direction);
    }
    HandOverSceneLights(this);
    Matrix4 view;
    view::CameraViewMatrix(view.m, TheCamera());   // (the result is not read)
    PopMatrix(device->stacks_[kStackView]);
    PushMatrix(device->stacks_[kStackView], view.m);
    const float* top = device->Top(kStackView)->m;
    device->PushMatrix(top, kStackScene2);
    device->PushMatrix(top, kStackScene1);
    PushMatrix(device->stacks_[kStackWorld], device->Top(kStackView)->m);
    g_sceneCamera = *device->Top(kStackScene1);
    g_haveSceneCamera = true;

    std::uint32_t flags = flags_;
    void* sky = skyDome_;
    if ((flags & 0x1000) && sky) {
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, g_stageBlocks[kSceneBlock]);
        SetRenderState(renderer, 14, 0);
        SetRenderState(renderer, 22, 1);
        SetRenderState(renderer, 29, 1);
        renderer->distanceFog_ = 0;
        skyDome_->Draw(TheSceneRenderer(), &camera_->localPosition_);
        renderer->distanceFog_ = 1;
        SetRenderState(renderer, 22, 2);
        SetRenderState(renderer, 29, 0);
        SetRenderState(renderer, 14, 1);
    }
    net::Pump();
    if ((flags_ & 0x100) && terrain_) {
        Vector3 frame;
        camera_->GroundPoint(&frame);
        CCamera* camera = camera_;
        terrain_->SetVisibleSectors(camera->localPosition_.x, camera->localPosition_.y, LandscapeDrawRadius());
        ApplyStateBlock(renderer, g_stageBlocks[kTerrainBlock]);
        terrain_->DrawGround(TheSceneRenderer());
    }
    DrawPass();
    net::Pump();
    if ((flags_ & 0x100) && terrain_) {
        Vector3 frame;
        camera_->GroundPoint(&frame);
        CCamera* camera = camera_;
        terrain_->SetVisibleSectors(camera->localPosition_.x, camera->localPosition_.y, LandscapeDrawRadius());
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, g_stageBlocks[kSceneBlock]);
        terrain_->DrawWater(TheSceneRenderer());
    }
    // The shadow map's casters the game did not draw (the map is fitted to the ground drawn above),
    // and the ground's own.
    if (flags_ & 0x100) HandOverTerrainCasters(this);
    HandOverOutsideCasters(this);
    net::Pump();
    if (flags_ & 0x400) {
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        SetRenderState(TheRenderDevice(), 27, 1);
        SetRenderState(TheRenderDevice(), 15, 0);
        SetRenderState(TheRenderDevice(), 19, 5);
        SetRenderState(TheRenderDevice(), 20, 6);
        SetRenderState(renderer, 14, 0);
        arrayManager_->Draw(TheSceneRenderer());
        SetRenderState(TheRenderDevice(), 19, 5);
        SetRenderState(TheRenderDevice(), 20, 6);
        SetRenderState(renderer, 14, 0);
        CParticleManager* particles = ClientParticleManager();
        particles->DrawAll(TheSceneRenderer());
        SetRenderState(TheRenderDevice(), 19, 5);
        SetRenderState(TheRenderDevice(), 20, 6);
        listManager_->DrawAll(TheSceneRenderer());
        SetRenderState(TheRenderDevice(), 19, 5);
        SetRenderState(TheRenderDevice(), 20, 6);
        rainSnow_.Draw(TheSceneRenderer());
        SetRenderState(renderer, 14, 1);
    }
    ApplyStateBlock(renderer, g_stageBlocks[kSceneBlock]);
    net::Pump();
    DrawLineLists();
    PopMatrix(device->stacks_[kStackWorld]);
    PopMatrix(device->stacks_[kStackScene2]);
    PopMatrix(device->stacks_[kStackScene1]);
    g_haveSceneCamera = false;
    if (flags_ & 0x2000) {
        SetRenderState(renderer, 7, 0);
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        renderer->distanceFog_ = 0;
        numbers_->Draw(TheSceneRenderer());
        renderer->distanceFog_ = 1;
        SetRenderState(renderer, 7, renderer->zEnable_);
    }
    render::EndWorld();
    return 1;
}

int CWorldClient::DrawLineLists() {
    for (auto* node = lineLists_.First(); node; node = lineLists_.After(node))
        node->value->Draw(ScenePipeline(), TheRenderDevice());
    return 1;
}

// ---- The debug display's switches -------------------------------------------------------------------

namespace {
constexpr std::uint32_t kShowFps = 1;
constexpr std::uint32_t kShowRenderInfo = 2;
constexpr std::uint32_t kShowManagers = 4;
constexpr std::uint32_t kShowWorldData = 8;

void RemoveDebugLine(const char* key) { DebugInfoPanel()->Remove(key); }
} // namespace

void CWorldClient::ShowFps() { debugFlags_ |= kShowFps; }

void CWorldClient::HideFps() {
    debugFlags_ &= ~kShowFps;
    RemoveDebugLine("FPS");
}

void CWorldClient::ShowRenderInfo() { debugFlags_ |= kShowRenderInfo; }

void CWorldClient::HideRenderInfo() {
    debugFlags_ &= ~kShowRenderInfo;
    RemoveDebugLine("triangles submitted");
    RemoveDebugLine("vertices submitted");
}

void CWorldClient::ShowManagers() { debugFlags_ |= kShowManagers; }

// Each manager's line is under its class name.
void CWorldClient::HideManagers() {
    debugFlags_ &= ~kShowManagers;
    RemoveDebugLine(manager3C_->GetClassName());
    RemoveDebugLine(manager40_->GetClassName());
    RemoveDebugLine(textureManager_->GetClassName());
    RemoveDebugLine(particleManager_->GetClassName());
    RemoveDebugLine(manager190_->GetClassName());
}

void CWorldClient::ShowWorldData() { debugFlags_ |= kShowWorldData; }

void CWorldClient::HideWorldData() {
    debugFlags_ &= ~kShowWorldData;
    RemoveDebugLine("Last Event");
    RemoveDebugLine("World Time");
}

int CWorldClient::DrawPass() {
    Terrain* terrain = terrain_;
    if (!terrain || !terrain->mapLoaded) return 0;
    CTextureDevice* renderer = TheRenderDevice();
    CTransformPipeline* device = ScenePipeline();
    std::uint32_t now = gameapi::timeGetTime();
    const Matrix4* view = device->Top(kStackView);
    FrameDeferredUnits()->Clear<stlp::HeldObject>();
    static const FrameList kCleared[] = {FrameList::Visible54,   FrameList::VisibleOther, FrameList::Visible5D,
                                         FrameList::VisibleUnits, FrameList::Visible52,   FrameList::Shadow54,
                                         FrameList::ShadowOther, FrameList::Shadow5D,     FrameList::ShadowUnits,
                                         FrameList::Shadow52,    FrameList::TouchedModels};
    for (FrameList list : kCleared) VectorClear(&FrameObjects(list));
    float animTime = AnimationTime();
    const float* position = &CameraAnchor(camera_)->x;
    if (bird_) bird_->Update(animTime);

    // The sectors around the camera: the position widened by the margin, clamped to the map
    // (size - 1) and to 0, in 16-unit sectors.
    const float margin = ObjectsDrawRadius();
    const float edge = MapEdgeMargin();
    terrain = terrain_;
    float yHi = SelectLess(AddRounded(position[1], margin), SubRounded(terrain->height, edge));
    float xMax = SubRounded(terrain_->width, edge);
    float xHi = SelectLess(AddRounded(position[0], margin), xMax);
    float yMax = SubRounded(terrain_->height, edge);
    float yLo = ClampToZero(Reload(SelectLess(SubRounded(position[1], margin), yMax)));
    float xMax2 = SubRounded(terrain_->width, edge);
    float xLo = ClampToZero(Reload(SelectLess(SubRounded(position[0], margin), xMax2)));
    std::uint32_t sx0 = static_cast<std::uint32_t>(Ftol(xLo)) >> 4;
    rangeFirstX_ = sx0;
    rangeX_ = sx0;
    std::uint32_t sy0 = static_cast<std::uint32_t>(Ftol(yLo)) >> 4;
    rangeFirstY_ = sy0;
    rangeY_ = sy0;
    rangeLastX_ = static_cast<std::uint32_t>(Ftol(xHi)) >> 4;
    rangeLastY_ = static_cast<std::uint32_t>(Ftol(yHi)) >> 4;

    int touched = 0;
    SectorList* sector = &objectGrid_.rows_[sy0][sx0];
    while (sector) {
        touched += CollectVisible(sector, view, animTime, now, 0, 0);
        std::uint32_t x = ++rangeX_;
        if (x > rangeLastX_) {
            rangeX_ = rangeFirstX_;
            std::uint32_t y = ++rangeY_;
            if (y > rangeLastY_) break;
        }
        sector = &objectGrid_.rows_[rangeY_][rangeX_];
    }
    touched += CollectVisible(FrameDeferredUnits(), view, animTime, now, 1, 0);
    EmptyDeferred();
    touched += CollectVisible(&heroes_, view, animTime, now, 1, 1);

    // Models in view stay loaded: the most recently used max(10, n / 8) lose flag 0x400.
    std::vector<CMapObject*>& models = FrameObjects(FrameList::TouchedModels);
    if (models.begin() != models.end()) {
        CMapObject** first = models.data();
        CMapObject** last = models.data() + models.size();
        IntroSortByModelUse(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, 0);
        FinalSortByModelUse(first, last, 0);
        int count = touched >> 3;
        if (count < 10) count = 10;
        for (CMapObject** p = models.data(); p != models.data() + models.size() && count != 0; --count) {
            CMapObject* object = *p++;
            object->flags_ &= ~0x400u;
        }
    }

    // The frame's window rectangles (an STLport list of 32-byte nodes).
    std::list<ScreenEntry>& queued = screenEntries_;
    if (flags_ & 0xC0) {
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        queued.clear();
        SetRenderState(renderer, 19, 5);
        SetRenderState(renderer, 20, 6);
        SetRenderState(renderer, 27, 1);
        SetRenderState(renderer, 15, 1);
    }
    if (flags_ & 0x40) {
        SortByModel(FrameList::Visible54);
        SortByModel(FrameList::VisibleOther);
        SortByModel(FrameList::Visible5D);
        SortByModel(FrameList::Visible52);
        SetRenderState(renderer, 24, 0xC8);
        DrawEach(FrameList::VisibleOther, kDraw);
        DrawEach(FrameList::Visible5D, kDrawUnit);
        CMapObject** end = FrameObjects(FrameList::Visible5D).data() + FrameObjects(FrameList::Visible5D).size();
        for (CMapObject** p = FrameObjects(FrameList::Visible5D).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
        DrawEach(FrameList::Visible54, kDraw);
        DrawEach(FrameList::Visible52, kDraw);
    }
    if ((flags_ & 0x80) && FrameObjects(FrameList::VisibleUnits).begin() != FrameObjects(FrameList::VisibleUnits).end()) {
        SortByModel(FrameList::VisibleUnits);
        SetRenderState(renderer, 24, 10);
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        DrawEach(FrameList::VisibleUnits, kDrawUnit);
        CMapObject** end = FrameObjects(FrameList::VisibleUnits).data() + FrameObjects(FrameList::VisibleUnits).size();
        for (CMapObject** p = FrameObjects(FrameList::VisibleUnits).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
    }

    // Light effects (three lists of the client, each behind its setting).
    if (!ListEmpty(bloodPrints_) || !ListEmpty(footprints_) || !ListEmpty(scorchMarks_)) {
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        if (LightEffects1B8()) DrawLightEffects(bloodPrints_);
        if (LightEffects1BC()) DrawLightEffects(footprints_);
        if (LightEffects1C0()) DrawLightEffects(scorchMarks_);
        SetRenderState(renderer, 14, 1);
    }

    // Shadows: projected along the camera's Z axis (its rotation) scaled by a constant.
    if (flags_ & 0x200) {
        SortByModel(FrameList::Shadow54);
        SortByModel(FrameList::ShadowOther);
        SortByModel(FrameList::Shadow5D);
        SortByModel(FrameList::Shadow52);
        Matrix4 matrix = {};
        matrix.m[15] = 1.0f;
        matrix.m[10] = 1.0f;
        matrix.m[5] = 1.0f;
        matrix.m[0] = 1.0f;
        Vector3 sun;
        TheCamera()->localRotation_.AxisZ(&sun);
        const float scale = ShadowLength();
        float product = sun.x * scale;
        matrix.m[3] = product;
        product = sun.y * scale;
        matrix.m[7] = product;
        product = sun.z * scale;
        matrix.m[11] = product;
        device->PushMatrix(matrix.m, kStackScene2);
        device->PushMatrix(matrix.m, kStackScene1);
        ApplyStateBlock(renderer, g_stageBlocks[kAlphaPassBlock]);
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        DrawEach(FrameList::Shadow5D, kDrawShadow);
        DrawEach(FrameList::ShadowOther, kDrawShadow);
        DrawEach(FrameList::Shadow54, kDrawShadow);
        DrawEach(FrameList::Shadow52, kDrawShadow);
        DrawEach(FrameList::ShadowUnits, kDrawShadow);
        SetRenderState(renderer, 14, 1);
        PopMatrix(device->stacks_[kStackScene2]);
        PopMatrix(device->stacks_[kStackScene1]);
    }

    // Selection markers (list7BC_ of object ids) in single-player mode 1 with units drawn: a marker at
    // each living object (at its named part when it has one); dead ones leave the list.
    std::list<int>& selected = list7BC_;
    if (!selected.empty() && GameMode() == 1 && (flags_ & 0x80)) {
        SetRenderState(renderer, 15, 0);
        TheSceneRenderer()->SetTexture(0, nullptr);
        CMapObjectManager* manager = manager40_;
        using Node = std::list<int>::iterator;
        for (Node node = selected.begin(); node != selected.end();) {
            Node next = std::next(node);
            int id = (*node);
            auto* object = manager->FindObjectAs<CMapObject>(static_cast<unsigned>(id));
            if (object) {
                if (object->IsDead() == 0) {
                    Vector3 point;
                    std::memcpy(&point, &object->localPosition_, sizeof(point));
                    if (CFigureMesh* part = object->FindPartByPrefix(MarkerPartName())) {
                        Vector3 scratch;
                        const float* at = object->PartWorldPosition(&scratch.x, part);
                        std::memcpy(&point, at, sizeof(point));
                        point.z = Reload(object->localPosition_.z);
                    }
                    const Quaternion* orientation = &object->localRotation_;
                    Vector3 axisY, axisX;
                    orientation->AxisY(&axisY);
                    orientation->AxisX(&axisX);
                    TheSceneRenderer()->DrawSelectionMarker(&point, 0.5f, &axisX, &axisY);
                } else {
                    if (object->flags_ & 0x10) {
                        object->flags_ &= ~0x110u;
                        // The side half's empty virtual and SetLit with 0: the marker light goes off.
                        object->attached->Unused25(0);
                        object->attached->SetLit(0);
                    }
                    selected.erase(node);
                }
            }
            node = next;
        }
    }

    EmptyDeferred();
    static const FrameList kCopied[] = {FrameList::Visible54,    FrameList::VisibleOther, FrameList::Visible5D,
                                        FrameList::VisibleUnits, FrameList::Visible52,    FrameList::Shadow54,
                                        FrameList::ShadowOther};
    for (FrameList list : kCopied) FrameObjects(list).clear();
    FrameObjects(FrameList::Shadow5D).clear();
    FrameObjects(FrameList::ShadowUnits).clear();
    FrameObjects(FrameList::Shadow52).clear();
    FrameObjects(FrameList::TouchedModels).clear();
    return 1;
}
