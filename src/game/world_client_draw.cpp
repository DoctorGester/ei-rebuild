// CWorldClient's draw path: Render (00569E40) and DrawPass (0056AFE0), with Rebirth's
// LandscapeDrawRadius and ObjectsDrawRadius behind their flags. See world.h and
// docs/notes/world.md.
//
// Both talk to Direct3D 7 through the renderer (RenderDeviceObject(), 007AF080: d3dDevice_ the
// IDirect3DDevice7, lastLoadResult_ the last state block result) and to the scene pipeline's matrix
// stacks (ScenePipeline(), 007AF068), exactly as the original: the same calls in the same order.
// Callees are reached through their original addresses (rewritten ones included, so the verify
// build's wrappers see the calls).
#include "ground_print.h"
#include "renderer_lines.h"
#include "net_session.h"
#include "sky_object.h"
#include "spell.h"   // CLight
#include "ui_flying_hit_points.h"
#include "view_matrices.h"
#include "world.h"

#include "camera.h"
#include "effects.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "model.h"
#include "terrain.h"
#include "texture.h"
#include "device.h"
#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "ui.h"
#include "world_deleting_destructors.h"
#include "world_effect.h"

#pragma warning(disable : 4740) // naked forwarders, inline assembly

// windows.h renames CEngineObject's GetClassName.
#undef GetClassName

namespace {

// IDirect3DDevice7 (the renderer's d3dDevice_): SetRenderState, ApplyStateBlock (its HRESULT goes
// to the renderer's lastLoadResult_, +0x08).
void SetRenderState(CTextureDevice* renderer, int state, std::uint32_t value) {
    device::SetRenderState(device::D3D(renderer), static_cast<D3DRENDERSTATETYPE>(state), value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
// The renderer's state block handles: 007AEDD0 the scene's, 007AEDD8 the alpha passes', 007AEDDC
// the terrain's.
const std::uint32_t& StateBlockScene() { return *reinterpret_cast<const std::uint32_t*>(0x007AEDD0); }
const std::uint32_t& StateBlockAlpha() { return *reinterpret_cast<const std::uint32_t*>(0x007AEDD8); }
const std::uint32_t& StateBlockTerrain() { return *reinterpret_cast<const std::uint32_t*>(0x007AEDDC); }
void ApplyStateBlock(CTextureDevice* renderer, const std::uint32_t& handle) {
    renderer->lastLoadResult_ = device::ApplyStateBlock(device::D3D(renderer), handle);
    render::state::Block(handle);
}

// 004E7840 / 004E7A40: the renderer flushes its two batched vertex buffers.
void Flush(CTextureDevice* renderer) {
    static_cast<CTextureDevice*>(renderer)->FlushBatch();
    static_cast<CTextureDevice*>(renderer)->FlushBatch2();
}

// The pipeline's matrix stacks: 1 and 2 pushed by 004D9B80 (PushSceneMatrixOriginal), 5 the world
// draw's, 9 the views.
constexpr int kStackScene1 = 1;    // +0x1C
constexpr int kStackScene2 = 2;    // +0x24
constexpr int kStackWorld = 5;     // +0x3C
constexpr int kStackView = 9;      // +0x5C

// Pushes `matrix` on `stack`, as the original inlines it.
void PushMatrix(MatrixStack& stack, const float* matrix) {
    int& depth = stack.count;
    Matrix4* matrices = stack.matrices;
    if (depth == 0) {
        depth = 1;
        std::memcpy(matrices, matrix, sizeof(Matrix4));
    } else {
        int d = ++depth;
        MatrixMultiplyOriginal(matrices[d - 1].m, nullptr, matrices[d - 2].m, matrix);
    }
}
void PopMatrix(MatrixStack& stack) {
    int& depth = stack.count;
    if (depth) --depth;
}

// Render's callees.
__declspec(naked) int __fastcall DrawPassAt(CWorldClient*) EI_ORIGINAL(0x0056AFE0)
__declspec(naked) void __fastcall LightningDraw(void*, void*, void*) EI_ORIGINAL(0x00468940)

// A starter setting (HKCU\Software\Gipat.Ru\EI_Starter\Settings, a DWORD, 100 when absent) as
// addon turns it into the float it patches in: (float)(unsigned)value, read once (addon reads its
// settings when it loads).
float StarterDrawRadius(const char* name) {
    DWORD value = 100;
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_READ, &key) == 0) {
        DWORD data = 0, size = sizeof(data), type = 0;
        if (RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&data), &size) == 0 && type == REG_DWORD)
            value = data;
        RegCloseKey(key);
    }
    float radius = static_cast<float>(static_cast<double>(value));
    return radius;
}

// The terrain's draw distance (the PUSH 100.0 of 00569E40).
float LandscapeDrawRadius() {
    static const float setting = StarterDrawRadius("LandscapeDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::LandscapeDrawRadius) ? setting : 100.0f;
}

// DrawPass's callees.
__declspec(naked) void __fastcall VectorClear(stlp::vector<CEngineObject*>*) EI_ORIGINAL(0x0056E870)
__declspec(naked) CEngineObject** __fastcall VectorCopyBack(CEngineObject**, CEngineObject**, CEngineObject**)
    EI_ORIGINAL(0x00571E30)

// The object margin around the camera when collecting what to draw (0073BAB0, 100.0).
float ObjectsDrawMargin() { return *reinterpret_cast<const float*>(0x0073BAB0); }
float ObjectsDrawRadius() {
    static const float setting = StarterDrawRadius("ObjectsDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::ObjectsDrawRadius) ? setting : ObjectsDrawMargin();
}
// 0073B84C: 1.0, the map edge's margin.
float MapEdgeMargin() { return *reinterpret_cast<const float*>(0x0073B84C); }
// 0073F750: the length of the shadows' projection along the camera's Z axis.
float ShadowLength() { return *reinterpret_cast<const float*>(0x0073F750); }
// The light effect settings (0079BD50, 0079BD4C, 0079BD54: the three lists +0x1B8 / +0x1BC /
// +0x1C0) and the game mode (0079BE64; 1 single player).
int LightEffectsSetting(std::uintptr_t address) { return *reinterpret_cast<const int*>(address); }
int LightEffects1B8() { return LightEffectsSetting(std::uintptr_t(0x0079BD50)); }
int LightEffects1BC() { return LightEffectsSetting(std::uintptr_t(0x0079BD4C)); }
int LightEffects1C0() { return LightEffectsSetting(std::uintptr_t(0x0079BD54)); }
int GameMode() { return *reinterpret_cast<const int*>(0x0079BE64); }
// 007900E4: the part a selection marker sits on.
const char* MarkerPartName() { return reinterpret_cast<const char*>(0x007900E4); }

// FLD a; FADD/FSUB b; FSTP: the result rounded to float.
float AddRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }
float SubRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
// FLD a; FCOMP b: whether C0 is set (a < b, or unordered).
bool LessOrUnordered(float a, float b) { return (x87::CompareStatus(a, b) & x87::kC0) != 0; }
// The original's mask selects: C0 picks a's bits, else b's (the smaller one) ...
float SelectLess(float a, float b) { return LessOrUnordered(a, b) ? a : b; }
// ... and, with FLD 0; FCOMP t, t when 0 < t (or unordered), else 0 (the larger one).
float ClampToZero(float t) { return LessOrUnordered(0.0f, t) ? t : 0.0f; }
// Copies a float through the FPU.
float Reload(float v) { return std::bit_cast<float>(x87::LoadStoreBits(&v)); }
// The game's _ftol of a float.
int Ftol(float v) { return static_cast<int>(gamecrt::Ftol(v)); }
// The client world's time: (float)(int)tick + the tick's fraction, rounded once (FILD, FADD, FSTP).
float AnimationTime() {
    const CWorldClient* world = UiClientWorld();
    return static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

// floor(log2(n)) as the original counts it (n >= 1).
int Log2(int n) {
    int depth = 0;
    while (n != 1) {
        n >>= 1;
        ++depth;
    }
    return depth;
}

// Sorts one draw list with UnitOrderLess. (The original pushes the renderer as the empty comparator,
// which nothing reads.)
void SortByModel(FrameList list) {
    stlp::vector<CEngineObject*>& v = FrameObjects(list);
    CEngineObject** first = v.begin();
    CEngineObject** last = v.end();
    if (first == last) return;
    IntroSortByModel(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, 0);
    FinalSortByModel(first, last, 0);
}

// Calls `draw` on every object of a draw list (in list order).
template <typename Draw>
void DrawEach(FrameList list, Draw draw) {
    CEngineObject** end = FrameObjects(list).end();
    for (CEngineObject** p = FrameObjects(list).begin(); p != end; ++p) draw(ObjectSlots(*p));
}
// The drawn objects' slots 22 / 23 / 24 (+0x58 / +0x5C / +0x60; 005783C0, 005783A0, 005783B0 in
// the old notes) with the scene context; their results are ignored. The new renderer learns which
// object the meshes of a figure draw belong to (its shadow map's casters, R16j).
constexpr auto kDraw = [](CMapObjectVirtuals* object) {
    render::BeginCaster(static_cast<const CEngineObject*>(object));
    object->DrawFigure(SceneContext());
    render::EndCaster();
};
constexpr auto kDrawUnit = [](CMapObjectVirtuals* object) {
    render::BeginCaster(static_cast<const CEngineObject*>(object));
    object->DrawCollectDirty(SceneContext());
    render::EndCaster();
};
constexpr auto kDrawShadow = [](CMapObjectVirtuals* object) { object->DrawFigureShadow(SceneContext()); };

// Empties the deferred unit list: each node's counted pointer released, the node freed (0056FB40
// or 0056F700).
void EmptyDeferred() {
    using Node = stlp::list<CEngineObject*>::Node;
    stlp::list<CEngineObject*>* deferred = FrameDeferredUnits();
    Node* head = deferred->head;
    Node* node = head->next;
    while (node != head) {
        Node* current = node;
        node = node->next;
        ObjectHolderDelete(&current->data, nullptr, 0);
        stlp::deallocate(current, sizeof(Node));
        head = deferred->head;
    }
    head->next = head;
    deferred->head->prev = deferred->head;
}

// Calls 006CEDE0 on the value of every node of an effect list (+0x1B8 / +0x1BC / +0x1C0).
template <typename T>
void DrawLightEffects(stlp::list<T>& list) {
    typename stlp::list<T>::Node* head = list.head;
    for (typename stlp::list<T>::Node* node = head->next; node != head; node = node->next) static_cast<GroundPrint*>(&node->data)->Draw();
}
template <typename T>
bool ListEmpty(const stlp::list<T>& list) { return list.head->next == list.head; }

} // namespace

int __fastcall QueueScreenEntry(CEngineObject* object, stlp::list<ScreenEntry>* queued) {
    if (!(object->flags_ & 0x20) || (object->flags_ & 0x40)) return 0;
    const CObject3D* figure = static_cast<const CObject3D*>(object);
    ScreenEntry entry;
    entry.classId = object->GetClassId();
    entry.objectId = static_cast<unsigned>(object->id_);
    for (int i = 0; i < 4; ++i) entry.rect[i] = figure->dirtyRect[i];
    stlp::list<ScreenEntry>::Insert(queued->head, entry);
    return 1;
}
EI_REPLACE(0x0056A370, QueueScreenEntry);

// ---- Render (00569E40) --------------------------------------------------------------------------

int CWorldClient::Render() {
    // The world view for the new renderer's smooth option (R12a: multisampled, models sampled
    // anisotropically); the interface draws after it.
    render::BeginWorld();
    // The sun the new renderer's shadow map is drawn from (R16j): the terrain's (Terrain::SetSun).
    if (terrain_ && terrain_->sun) {
        const Vector3& d = terrain_->sun->direction_;
        const float direction[3] = {d.x, d.y, d.z};
        render::ShadowSun(direction);
    }
    CTextureDevice* renderer = RenderDeviceObject();
    CTransformPipeline* device = ScenePipeline();
    UpdateLighting();
    Flush(renderer);
    Matrix4 view;
    view::CameraViewMatrix(view.m, camera::TheCamera());   // 004F0EF0 (thiscall on the pipeline, not read)
    PopMatrix(device->stacks_[kStackView]);
    PushMatrix(device->stacks_[kStackView], view.m);
    const float* top = device->Top(kStackView)->m;
    PushSceneMatrixOriginal(device, nullptr, top, kStackScene2);
    PushSceneMatrixOriginal(device, nullptr, top, kStackScene1);
    PushMatrix(device->stacks_[kStackWorld], device->Top(kStackView)->m);

    std::uint32_t flags = flags_;
    void* sky = skyDome_;
    if ((flags & 0x1000) && sky) {
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, StateBlockScene());
        SetRenderState(renderer, 14, 0);
        SetRenderState(renderer, 22, 1);
        SetRenderState(renderer, 29, 1);
        renderer->distanceFog_ = 0;
        static_cast<CSky*>(skyDome_)->Draw(static_cast<CSceneRenderer*>(SceneContext()), static_cast<const Vector3*>(&camera_->localPosition_));
        Flush(renderer);
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
        ApplyStateBlock(renderer, StateBlockTerrain());
        terrain_->DrawGround(static_cast<CSceneRenderer*>(SceneContext()));
        Flush(renderer);
    }
    DrawPassAt(this);
    net::Pump();
    if ((flags_ & 0x100) && terrain_) {
        Vector3 frame;
        camera_->GroundPoint(&frame);
        CCamera* camera = camera_;
        terrain_->SetVisibleSectors(camera->localPosition_.x, camera->localPosition_.y, LandscapeDrawRadius());
        Flush(renderer);
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, StateBlockScene());
        terrain_->DrawWater(static_cast<CSceneRenderer*>(SceneContext()));
        Flush(renderer);
    }
    net::Pump();
    if (flags_ & 0x400) {
        Flush(renderer);
        ApplyStateBlock(renderer, StateBlockAlpha());
        SetRenderState(RenderDeviceObject(), 27, 1);
        SetRenderState(RenderDeviceObject(), 15, 0);
        SetRenderState(RenderDeviceObject(), 19, 5);
        SetRenderState(RenderDeviceObject(), 20, 6);
        SetRenderState(renderer, 14, 0);
        static_cast<ClientArrayManager*>(arrayManager_)->DrawImpl(static_cast<CSceneRenderer*>(SceneContext()));
        Flush(renderer);
        SetRenderState(RenderDeviceObject(), 19, 5);
        SetRenderState(RenderDeviceObject(), 20, 6);
        SetRenderState(renderer, 14, 0);
        CParticleManager* particles = ClientParticleManager();
        static_cast<CParticleManager*>(particles)->DrawAll(SceneContext());
        Flush(renderer);
        SetRenderState(RenderDeviceObject(), 19, 5);
        SetRenderState(RenderDeviceObject(), 20, 6);
        LightningDraw(listManager_, nullptr, SceneContext());
        Flush(renderer);
        SetRenderState(RenderDeviceObject(), 19, 5);
        SetRenderState(RenderDeviceObject(), 20, 6);
        static_cast<CRainSnow*>(RainSnow())->DrawImpl(static_cast<CSceneRenderer*>(SceneContext()));
        Flush(renderer);
        SetRenderState(renderer, 14, 1);
    }
    ApplyStateBlock(renderer, StateBlockScene());
    net::Pump();
    DrawLineLists();
    PopMatrix(device->stacks_[kStackWorld]);
    PopMatrix(device->stacks_[kStackScene2]);
    PopMatrix(device->stacks_[kStackScene1]);
    if (flags_ & 0x2000) {
        SetRenderState(renderer, 7, 0);
        ApplyStateBlock(renderer, StateBlockAlpha());
        renderer->distanceFog_ = 0;
        static_cast<CFlyingHitPoints*>(numbers_)->Draw(static_cast<CSceneRenderer*>(SceneContext()));
        renderer->distanceFog_ = 1;
        Flush(renderer);
        SetRenderState(renderer, 7, renderer->zEnable_);
    }
    render::EndWorld();
    return 1;
}
EI_REPLACE_METHOD(0x00569E40, CWorldClient::Render);

// 00569D60 (A-W13).
int CWorldClient::DrawLineLists() {
    for (auto* node = lineLists_.First(); node; node = lineLists_.After(node))
        static_cast<LineBatch*>(node->value)->Draw(ScenePipeline(), RenderDeviceObject());
    return 1;
}
EI_REPLACE_METHOD(0x00569D60, CWorldClient::DrawLineLists);

// ---- The debug display's switches -------------------------------------------------------------------

namespace {
constexpr std::uint32_t kShowFps = 1;
constexpr std::uint32_t kShowRenderInfo = 2;
constexpr std::uint32_t kShowManagers = 4;
constexpr std::uint32_t kShowWorldData = 8;

// The debug info panel (0079B930, what 004447D0 returns) loses the line `key` (its slot 1).
void RemoveDebugLine(const char* key) { strmap::StringTableRemove(DebugInfoPanel(), nullptr, key); }
} // namespace

void CWorldClient::ShowFps() { debugFlags_ |= kShowFps; }
EI_REPLACE_METHOD(0x00569A10, CWorldClient::ShowFps);

void CWorldClient::HideFps() {
    debugFlags_ &= ~kShowFps;
    RemoveDebugLine("FPS");
}
EI_REPLACE_METHOD(0x00569A20, CWorldClient::HideFps);

void CWorldClient::ShowRenderInfo() { debugFlags_ |= kShowRenderInfo; }
EI_REPLACE_METHOD(0x00569A40, CWorldClient::ShowRenderInfo);

void CWorldClient::HideRenderInfo() {
    debugFlags_ &= ~kShowRenderInfo;
    RemoveDebugLine("triangles submitted");
    RemoveDebugLine("vertices submitted");
}
EI_REPLACE_METHOD(0x00569A50, CWorldClient::HideRenderInfo);

void CWorldClient::ShowManagers() { debugFlags_ |= kShowManagers; }
EI_REPLACE_METHOD(0x00569A90, CWorldClient::ShowManagers);

// Each manager's line is under its class name.
void CWorldClient::HideManagers() {
    debugFlags_ &= ~kShowManagers;
    RemoveDebugLine(manager3C_->GetClassName());
    RemoveDebugLine(manager40_->GetClassName());
    RemoveDebugLine(textureManager_->GetClassName());
    RemoveDebugLine(particleManager_->GetClassName());
    RemoveDebugLine(manager190_->GetClassName());
}
EI_REPLACE_METHOD(0x00569AA0, CWorldClient::HideManagers);

void CWorldClient::ShowWorldData() { debugFlags_ |= kShowWorldData; }
EI_REPLACE_METHOD(0x00569B10, CWorldClient::ShowWorldData);

void CWorldClient::HideWorldData() {
    debugFlags_ &= ~kShowWorldData;
    RemoveDebugLine("Last Event");
    RemoveDebugLine("World Time");
}
EI_REPLACE_METHOD(0x00569B20, CWorldClient::HideWorldData);

// ---- DrawPass (0056AFE0) ------------------------------------------------------------------------

int CWorldClient::DrawPass() {
    Terrain* terrain = terrain_;
    if (!terrain || !terrain->mapLoaded) return 0;
    CTextureDevice* renderer = RenderDeviceObject();
    CTransformPipeline* device = ScenePipeline();
    std::uint32_t now = gameapi::timeGetTime();
    const Matrix4* view = device->Top(kStackView);
    CountedListClearOriginal(FrameDeferredUnits());
    static const FrameList kCleared[] = {FrameList::Visible54,   FrameList::VisibleOther, FrameList::Visible5D,
                                         FrameList::VisibleUnits, FrameList::Visible52,   FrameList::Shadow54,
                                         FrameList::ShadowOther, FrameList::Shadow5D,     FrameList::ShadowUnits,
                                         FrameList::Shadow52,    FrameList::TouchedModels};
    for (FrameList list : kCleared) VectorClear(&FrameObjects(list));
    float animTime = AnimationTime();
    const float* position = &CameraAnchor(camera_)->x;
    if (void* listener = owned1B0_) static_cast<CEffectMoshka*>(listener)->Update(animTime);

    // The sectors around the camera: the position widened by the margin, clamped to the map
    // (size - 0073B84C) and to 0, in 16-unit sectors.
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
    stlp::vector<CEngineObject*>& models = FrameObjects(FrameList::TouchedModels);
    if (models.begin() != models.end()) {
        CEngineObject** first = models.begin();
        CEngineObject** last = models.end();
        IntroSortByModelUse(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, 0);
        FinalSortByModelUse(first, last, 0);
        int count = touched >> 3;
        if (count < 10) count = 10;
        for (CEngineObject** p = models.begin(); p != models.end() && count != 0; --count) {
            CEngineObject* object = *p++;
            object->flags_ &= ~0x400u;
        }
    }

    // The frame's window rectangles (an STLport list of 32-byte nodes).
    stlp::list<ScreenEntry>& queued = screenEntries_;
    if (flags_ & 0xC0) {
        ApplyStateBlock(renderer, StateBlockAlpha());
        using Node = stlp::list<ScreenEntry>::Node;
        Node* node = queued.head->next;
        while (node != queued.head) {
            Node* current = node;
            node = node->next;
            stlp::deallocate(current, 32);   // 0056F840
        }
        queued.head->next = queued.head;
        queued.head->prev = queued.head;
        Flush(renderer);
        SetRenderState(renderer, 19, 5);
        SetRenderState(renderer, 20, 6);
        SetRenderState(renderer, 27, 1);
        SetRenderState(renderer, 15, 1);
        Flush(renderer);
    }
    if (flags_ & 0x40) {
        SortByModel(FrameList::Visible54);
        SortByModel(FrameList::VisibleOther);
        SortByModel(FrameList::Visible5D);
        SortByModel(FrameList::Visible52);
        SetRenderState(renderer, 24, 0xC8);
        DrawEach(FrameList::VisibleOther, kDraw);
        DrawEach(FrameList::Visible5D, kDrawUnit);
        CEngineObject** end = FrameObjects(FrameList::Visible5D).end();
        for (CEngineObject** p = FrameObjects(FrameList::Visible5D).begin(); p != end; ++p) QueueScreenEntry(*p, &queued);
        Flush(renderer);
        DrawEach(FrameList::Visible54, kDraw);
        DrawEach(FrameList::Visible52, kDraw);
        Flush(renderer);
    }
    if ((flags_ & 0x80) && FrameObjects(FrameList::VisibleUnits).begin() != FrameObjects(FrameList::VisibleUnits).end()) {
        SortByModel(FrameList::VisibleUnits);
        SetRenderState(renderer, 24, 10);
        ApplyStateBlock(renderer, StateBlockAlpha());
        DrawEach(FrameList::VisibleUnits, kDrawUnit);
        CEngineObject** end = FrameObjects(FrameList::VisibleUnits).end();
        for (CEngineObject** p = FrameObjects(FrameList::VisibleUnits).begin(); p != end; ++p) QueueScreenEntry(*p, &queued);
        Flush(renderer);
    }

    // Light effects (three lists of the client, each behind its setting).
    if (!ListEmpty(bloodPrints_) || !ListEmpty(footprints_) || !ListEmpty(scorchMarks_)) {
        ApplyStateBlock(renderer, StateBlockAlpha());
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        if (LightEffects1B8()) DrawLightEffects(bloodPrints_);
        if (LightEffects1BC()) DrawLightEffects(footprints_);
        if (LightEffects1C0()) DrawLightEffects(scorchMarks_);
        SetRenderState(renderer, 14, 1);
    }

    // Shadows: projected along the camera's Z axis (its rotation, 0079B2DC) scaled by 0073F750.
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
        QuaternionAxisZ(&camera::TheCamera()->localRotation_, nullptr, &sun);
        const float scale = ShadowLength();
        float product = sun.x * scale;
        matrix.m[3] = product;
        product = sun.y * scale;
        matrix.m[7] = product;
        product = sun.z * scale;
        matrix.m[11] = product;
        PushSceneMatrixOriginal(device, nullptr, matrix.m, kStackScene2);
        PushSceneMatrixOriginal(device, nullptr, matrix.m, kStackScene1);
        ApplyStateBlock(renderer, StateBlockAlpha());
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        DrawEach(FrameList::Shadow5D, kDrawShadow);
        DrawEach(FrameList::ShadowOther, kDrawShadow);
        DrawEach(FrameList::Shadow54, kDrawShadow);
        DrawEach(FrameList::Shadow52, kDrawShadow);
        DrawEach(FrameList::ShadowUnits, kDrawShadow);
        Flush(renderer);
        SetRenderState(renderer, 14, 1);
        PopMatrix(device->stacks_[kStackScene2]);
        PopMatrix(device->stacks_[kStackScene1]);
    }

    // Selection markers (list +0x7BC of object ids) in single-player mode 1 with units drawn: a
    // marker at each living object (at its named part when it has one); dead ones leave the list.
    stlp::list<void*>& selected = list7BC_;
    if (!ListEmpty(selected) && GameMode() == 1 && (flags_ & 0x80)) {
        SetRenderState(renderer, 15, 0);
        static_cast<CSceneRenderer*>(SceneContext())->SetTextureImpl(0, nullptr);
        CEngineObject* manager = manager40_;
        using Node = stlp::list<void*>::Node;
        Node* head = selected.head;
        for (Node* node = head->next; node != head;) {
            Node* next = node->next;
            int id = static_cast<int>(reinterpret_cast<std::intptr_t>(node->data));
            auto* object = static_cast<CMapObject*>(ManagerFindObject(manager, nullptr, id));
            if (object) {
                if (ObjectSlots(object)->IsDead() == 0) {
                    Vector3 point;
                    std::memcpy(&point, &object->position, sizeof(point));
                    if (void* part = static_cast<CObject3D*>(object)->FindPartByPrefix(MarkerPartName())) {
                        Vector3 scratch;
                        const float* at = object->PartWorldPosition(&scratch.x, static_cast<const CFigureMesh*>(part));
                        std::memcpy(&point, at, sizeof(point));
                        point.z = Reload(object->position.z);
                    }
                    const Quaternion* orientation = &object->orientation;
                    Vector3 axisY, axisX;
                    QuaternionAxisY(orientation, nullptr, &axisY);
                    QuaternionAxisX(orientation, nullptr, &axisX);
                    static_cast<CSceneRenderer*>(SceneContext())->DrawSelectionMarker(&point, 0.5f, &axisX, &axisY);
                } else {
                    if (object->flags_ & 0x10) {
                        object->flags_ &= ~0x110u;
                        // The side half's slots 25 (empty) and 26 with 0: the marker light goes off.
                        static_cast<CObject3DSpecific*>(object->attached)->Unused25(0);
                        static_cast<CObject3DSpecific*>(object->attached)->SetLit(0);
                    }
                    void* unused;
                    IdListEraseOriginal(&selected, nullptr, &unused, node);
                }
            }
            node = next;
        }
    }

    EmptyDeferred();
    static const FrameList kCopied[] = {FrameList::Visible54,    FrameList::VisibleOther, FrameList::Visible5D,
                                        FrameList::VisibleUnits, FrameList::Visible52,    FrameList::Shadow54,
                                        FrameList::ShadowOther};
    for (FrameList list : kCopied) {
        stlp::vector<CEngineObject*>& v = FrameObjects(list);
        v.Finish() = VectorCopyBack(v.end(), v.end(), v.begin());
    }
    stlp::vector<CEngineObject*>& shadow5D = FrameObjects(FrameList::Shadow5D);
    shadow5D.EraseRange<stlp::PlainElement<CEngineObject*>>(shadow5D.begin(), shadow5D.end());
    stlp::vector<CEngineObject*>& shadowUnits = FrameObjects(FrameList::ShadowUnits);
    shadowUnits.EraseRange<stlp::PlainElement<CEngineObject*>>(shadowUnits.begin(), shadowUnits.end());
    stlp::vector<CEngineObject*>& shadow52 = FrameObjects(FrameList::Shadow52);
    shadow52.Finish() = VectorCopyBack(shadow52.end(), shadow52.end(), shadow52.begin());
    stlp::vector<CEngineObject*>& touchedModels = FrameObjects(FrameList::TouchedModels);
    touchedModels.EraseRange<stlp::PlainElement<CEngineObject*>>(touchedModels.begin(), touchedModels.end());
    return 1;
}
EI_REPLACE_METHOD(0x0056AFE0, CWorldClient::DrawPass);
