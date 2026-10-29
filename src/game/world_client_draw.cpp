// CWorldClient's draw path: Render and DrawPass, with Rebirth's LandscapeDrawRadius and ObjectsDrawRadius
// behind their flags. See world.h and docs/notes/world.md. Both set the renderer's render state
// (render/render_state.h) and the scene pipeline's matrix stacks (g_sceneRenderer.pipeline_) in the original's
// order.
#include "address_order.h"
#include "camera.h"
#include "effects.h"
#include "figure_manager.h"
#include "game_data.h"
#include "game_singletons.h"
#include "ground_print.h"
#include "lightning.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "model.h"
#include "net_session.h"
#include "options.h"
#include "render/render.h"
#include "render/render_state.h"
#include "renderer_lines.h"
#include "settings_file.h"
#include "sky_object.h"
#include "spell.h"   // CLight
#include "system_text.h"
#include "terrain.h"
#include "texture.h"
#include "ui.h"
#include "ui_flying_hit_points.h"
#include "view_matrices.h"
#include "world.h"
#include "world_client_shadow_casters.h"
#include "world_data.h"
#include "world_effect.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/gameapi.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <vector>
#include <windows.h>

namespace {

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
    Matrix4* matrices = stack.matrices.data();
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
constexpr float kObjectsDrawMargin = 100.0f;
float ObjectsDrawRadius() {
    static const float setting = StarterDrawRadius("ObjectsDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::ObjectsDrawRadius) ? setting : kObjectsDrawMargin;
}
constexpr float kMapEdgeMargin = 1.0f;
// The length of the shadows' projection along the camera's Z axis.
constexpr float kShadowLength = -0.02f;
// The light effect settings (the blood print, footprint and scorch mark lists) and the game mode (1
// single player).
int LightEffectsSetting(const int& setting) { return setting; }
int LightEffects1B8() { return LightEffectsSetting(options::g_options.enableBloodprints_); }
int LightEffects1BC() { return LightEffectsSetting(options::g_options.enableFootprints_); }
int LightEffects1C0() { return LightEffectsSetting(options::g_options.enableFireprints_); }
const char* MarkerPartName() { return "hp"; }

float AddRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) + b); }
float SubRounded(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }
// a < b, or unordered.
bool Less(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b;
}
// t when 0 < t or unordered, else 0.
// A float copied through the FPU in the original; here the value as it is.
float Reload(float v) { return v; }
int Ftol(float v) { return static_cast<int>(libcrt::Ftol(v)); }
// The client world's time: (float)(int)tick + the tick's fraction, rounded once.
float AnimationTime() {
    const CWorldClient* world = &g_clientWorld;
    return static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

// Sorts one draw list by the first part set's texture, then the figure, then the view depth (the game's
// comparator compares the texture and the figure by address; ours by name: address_order::DrawBefore).
// Objects with the same keys are drawn in the game's sort's order.
void SortByModel(FrameList list) {
    std::vector<CMapObject*>& v = FrameObjects(list);
    lib::IntroSort(v.data(), v.data() + v.size(),
                   [](CMapObject* a, CMapObject* b) { return address_order::DrawBefore(a, b); });
}

template <typename Draw>
void DrawEach(FrameList list, Draw draw) {
    for (CMapObject* object : FrameObjects(list)) draw(object);
}
// The drawn objects' three figure draw virtuals with the scene context; their results are ignored. The
// new renderer learns which object the meshes of a figure draw belong to (its shadow map's casters).
constexpr auto kDraw = [](CMapObject* object) {
    render::BeginCaster(object, false);
    object->DrawFigure(&g_sceneRenderer);
    render::EndCaster();
};
constexpr auto kDrawUnit = [](CMapObject* object) {
    render::BeginCaster(object, true);
    object->DrawCollectDirty(&g_sceneRenderer);
    render::EndCaster();
};
constexpr auto kDrawShadow = [](CMapObject* object) { object->DrawFigureShadow(&g_sceneRenderer); };

// Calls the ground print draw on the value of every node of an effect list (the ground print lists).
template <typename T>
void DrawLightEffects(std::list<T>& list) {
    for (GroundPrint& print : list) print.Draw();
}

} // namespace

const Matrix4* worlddata::SceneCamera() { return g_haveSceneCamera ? &g_sceneCamera : nullptr; }

int QueueScreenEntry(CMapObject* object, std::list<ScreenEntry>* queued) {
    if (!(object->flags_ & MapObjectFlag::Shown) || (object->flags_ & MapObjectFlag::NotDrawn)) return 0;
    const CObject3D* figure = object;
    ScreenEntry entry;
    entry.classId = object->GetClassId();
    entry.objectId = static_cast<unsigned>(object->id_);
    entry.rect = figure->dirtyRect;
    queued->insert(queued->end(), entry);
    return 1;
}

int CWorldClient::Render() {
    render::RenderState& state = render::g_renderState;
    // The world view for the new renderer's render quality (multisampled, models sampled
    // anisotropically); the interface draws after it.
    render::BeginWorld();
    CTextureDevice* renderer = g_sceneRenderer.device_;
    CTransformPipeline* device = g_sceneRenderer.pipeline_;
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
    // The smooth view (an experiment, render::SmoothView): the pose between the camera's last two ticks.
    const bool smoothView = render::SmoothView() != render::ViewSmoothing::Off;
    const camera::ViewPose drawn = smoothView ? camera::DrawnView(&g_camera) : camera::ViewPose{};
    if (smoothView) view::ViewMatrix(view.m, drawn.position, drawn.rotation);
    else view::CameraViewMatrix(view.m, &g_camera);   // (the result is not read)
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
    if ((flags & WorldClientFlag::DrawSky) && sky) {
        state.alphaTest = false;
        state.stages[0].combiner = kSceneStage;
        state.depthWrite = false;
        state.cull = render::Cull::None;
        state.specular = true;
        renderer->distanceFog_ = 0;
        skyDome_->Draw(&g_sceneRenderer, smoothView ? &drawn.position : &camera_->localPosition_);
        renderer->distanceFog_ = 1;
        state.cull = render::Cull::Clockwise;
        state.specular = false;
        state.depthWrite = true;
    }
    net::Pump();
    if ((flags_ & WorldClientFlag::DrawTerrain) && terrain_) {
        Vector3 frame;
        camera_->GroundPoint(&frame);
        CCamera* camera = camera_;
        terrain_->SetVisibleSectors(camera->localPosition_.x, camera->localPosition_.y, LandscapeDrawRadius());
        state.stages[0].combiner = kTerrainStage;
        terrain_->DrawGround(&g_sceneRenderer);
    }
    DrawPass();
    net::Pump();
    if ((flags_ & WorldClientFlag::DrawTerrain) && terrain_) {
        Vector3 frame;
        camera_->GroundPoint(&frame);
        CCamera* camera = camera_;
        terrain_->SetVisibleSectors(camera->localPosition_.x, camera->localPosition_.y, LandscapeDrawRadius());
        state.alphaTest = false;
        state.stages[0].combiner = kSceneStage;
        terrain_->DrawWater(&g_sceneRenderer);
    }
    // The shadow map's casters the game did not draw (the map is fitted to the ground drawn above),
    // and the ground's own.
    if (flags_ & WorldClientFlag::DrawTerrain) HandOverTerrainCasters(this);
    HandOverOutsideCasters(this);
    net::Pump();
    if (flags_ & WorldClientFlag::DrawEffects) {
        state.stages[0].combiner = kAlphaPassStage;
        state.blend = true;
        state.alphaTest = false;
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = false;
        arrayManager_->Draw(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = false;
        CParticleManager* particles = ClientParticleManager();
        particles->DrawAll(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        listManager_->DrawAll(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        rainSnow_.Draw(&g_sceneRenderer);
        state.depthWrite = true;
    }
    state.stages[0].combiner = kSceneStage;
    net::Pump();
    DrawLineLists();
    PopMatrix(device->stacks_[kStackWorld]);
    PopMatrix(device->stacks_[kStackScene2]);
    PopMatrix(device->stacks_[kStackScene1]);
    g_haveSceneCamera = false;
    if (flags_ & WorldClientFlag::DrawNumbers) {
        state.depthTest = false;
        state.stages[0].combiner = kAlphaPassStage;
        renderer->distanceFog_ = 0;
        numbers_->Draw(&g_sceneRenderer);
        renderer->distanceFog_ = 1;
        state.depthTest = true;
    }
    render::EndWorld();
    return 1;
}

int CWorldClient::DrawLineLists() {
    for (auto& node : lineLists_) node.second->Draw(g_sceneRenderer.pipeline_, g_sceneRenderer.device_);
    return 1;
}

int CWorldClient::DrawPass() {
    render::RenderState& state = render::g_renderState;
    Terrain* terrain = terrain_;
    if (!terrain || !terrain->mapLoaded) return 0;
    CTransformPipeline* device = g_sceneRenderer.pipeline_;
    std::uint32_t now = gameapi::timeGetTime();
    const Matrix4* view = device->Top(kStackView);
    g_deferredUnits.clear();
    static const FrameList kCleared[] = {FrameList::VisibleBuildings, FrameList::VisibleOther, FrameList::VisibleLevers,
                                         FrameList::VisibleUnits, FrameList::VisibleTrees, FrameList::ShadowBuildings,
                                         FrameList::ShadowOther, FrameList::ShadowLevers, FrameList::ShadowUnits,
                                         FrameList::ShadowTrees, FrameList::TouchedModels};
    for (FrameList list : kCleared) VectorClear(&FrameObjects(list));
    float animTime = AnimationTime();
    const float* position = &CameraAnchor(camera_)->x;
    if (bird_) bird_->Update(animTime);

    // The sectors around the camera: the position widened by the margin, clamped to the map
    // (size - 1) and to 0, in 16-unit sectors.
    const float margin = ObjectsDrawRadius();
    const float edge = kMapEdgeMargin;
    terrain = terrain_;
    float yHi = MinKeepB(AddRounded(position[1], margin), SubRounded(terrain->height, edge));
    float xMax = SubRounded(terrain_->width, edge);
    float xHi = MinKeepB(AddRounded(position[0], margin), xMax);
    float yMax = SubRounded(terrain_->height, edge);
    float yLo = MaxKeepA(0.0f, Reload(MinKeepB(SubRounded(position[1], margin), yMax)));
    float xMax2 = SubRounded(terrain_->width, edge);
    float xLo = MaxKeepA(0.0f, Reload(MinKeepB(SubRounded(position[0], margin), xMax2)));
    std::uint32_t sx0 = static_cast<std::uint32_t>(Ftol(xLo)) >> 4;
    rangeFirstX_ = sx0;
    rangeX_ = sx0;
    std::uint32_t sy0 = static_cast<std::uint32_t>(Ftol(yLo)) >> 4;
    rangeFirstY_ = sy0;
    rangeY_ = sy0;
    rangeLastX_ = static_cast<std::uint32_t>(Ftol(xHi)) >> 4;
    rangeLastY_ = static_cast<std::uint32_t>(Ftol(yHi)) >> 4;

    int touched = 0;
    std::list<HeldRef<CMapObject>>* sector = &objectGrid_.rows_[sy0][sx0];
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
    touched += CollectVisible(&g_deferredUnits, view, animTime, now, 1, 0);
    g_deferredUnits.clear();
    touched += CollectVisible(&heroes_, view, animTime, now, 1, 1);

    // Models in view stay loaded: the most recently used max(10, n / 8) lose MapObjectFlag::Static.
    std::vector<CMapObject*>& models = FrameObjects(FrameList::TouchedModels);
    if (models.begin() != models.end()) {
        // The most recently used first; the models used in the same frame stay in the game's sort's order.
        lib::IntroSort(models.data(), models.data() + models.size(), [](CMapObject* a, CMapObject* b) {
            return b->ClientSpecific()->touchTime < a->ClientSpecific()->touchTime;
        });
        int count = touched >> 3;
        if (count < 10) count = 10;
        for (CMapObject** p = models.data(); p != models.data() + models.size() && count != 0; --count) {
            CMapObject* object = *p++;
            object->flags_ &= ~MapObjectFlag::Static;
        }
    }

    // The frame's window rectangles (an STLport list of 32-byte nodes).
    std::list<ScreenEntry>& queued = screenEntries_;
    if (flags_ & (WorldClientFlag::DrawObjects | WorldClientFlag::DrawUnits)) {
        state.stages[0].combiner = kAlphaPassStage;
        queued.clear();
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.blend = true;
        state.alphaTest = true;
    }
    if (flags_ & WorldClientFlag::DrawObjects) {
        SortByModel(FrameList::VisibleBuildings);
        SortByModel(FrameList::VisibleOther);
        SortByModel(FrameList::VisibleLevers);
        SortByModel(FrameList::VisibleTrees);
        state.alphaReference = 0xC8;
        DrawEach(FrameList::VisibleOther, kDraw);
        DrawEach(FrameList::VisibleLevers, kDrawUnit);
        CMapObject** end = FrameObjects(FrameList::VisibleLevers).data() + FrameObjects(FrameList::VisibleLevers).size();
        for (CMapObject** p = FrameObjects(FrameList::VisibleLevers).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
        DrawEach(FrameList::VisibleBuildings, kDraw);
        DrawEach(FrameList::VisibleTrees, kDraw);
    }
    if ((flags_ & WorldClientFlag::DrawUnits) && FrameObjects(FrameList::VisibleUnits).begin() != FrameObjects(FrameList::VisibleUnits).end()) {
        SortByModel(FrameList::VisibleUnits);
        state.alphaReference = 10;
        state.stages[0].combiner = kAlphaPassStage;
        DrawEach(FrameList::VisibleUnits, kDrawUnit);
        CMapObject** end = FrameObjects(FrameList::VisibleUnits).data() + FrameObjects(FrameList::VisibleUnits).size();
        for (CMapObject** p = FrameObjects(FrameList::VisibleUnits).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
    }

    // Light effects (three lists of the client, each behind its setting).
    if (!bloodPrints_.empty() || !footprints_.empty() || !scorchMarks_.empty()) {
        state.stages[0].combiner = kAlphaPassStage;
        state.alphaTest = false;
        state.depthWrite = false;
        if (LightEffects1B8()) DrawLightEffects(bloodPrints_);
        if (LightEffects1BC()) DrawLightEffects(footprints_);
        if (LightEffects1C0()) DrawLightEffects(scorchMarks_);
        state.depthWrite = true;
    }

    // Shadows: projected along the camera's Z axis (its rotation) scaled by a constant.
    if (flags_ & WorldClientFlag::DrawShadows) {
        SortByModel(FrameList::ShadowBuildings);
        SortByModel(FrameList::ShadowOther);
        SortByModel(FrameList::ShadowLevers);
        SortByModel(FrameList::ShadowTrees);
        Matrix4 matrix = {};
        matrix.m[15] = 1.0f;
        matrix.m[10] = 1.0f;
        matrix.m[5] = 1.0f;
        matrix.m[0] = 1.0f;
        Vector3 sun;
        g_camera.localRotation_.AxisZ(&sun);
        const float scale = kShadowLength;
        float product = sun.x * scale;
        matrix.m[3] = product;
        product = sun.y * scale;
        matrix.m[7] = product;
        product = sun.z * scale;
        matrix.m[11] = product;
        device->PushMatrix(matrix.m, kStackScene2);
        device->PushMatrix(matrix.m, kStackScene1);
        state.stages[0].combiner = kAlphaPassStage;
        state.alphaTest = false;
        state.depthWrite = false;
        DrawEach(FrameList::ShadowLevers, kDrawShadow);
        DrawEach(FrameList::ShadowOther, kDrawShadow);
        DrawEach(FrameList::ShadowBuildings, kDrawShadow);
        DrawEach(FrameList::ShadowTrees, kDrawShadow);
        DrawEach(FrameList::ShadowUnits, kDrawShadow);
        state.depthWrite = true;
        PopMatrix(device->stacks_[kStackScene2]);
        PopMatrix(device->stacks_[kStackScene1]);
    }

    // Selection markers (selectedIds_ of object ids) in single-player mode 1 with units drawn: a marker at
    // each living object (at its named part when it has one); dead ones leave the list.
    std::list<int>& selected = selectedIds_;
    if (!selected.empty() && options::g_options.selectionType_ == 1 && (flags_ & WorldClientFlag::DrawUnits)) {
        state.alphaTest = false;
        g_sceneRenderer.SetTexture(0, nullptr);
        CMapObjectManager* manager = objectManager_;
        for (std::list<int>::iterator node = selected.begin(); node != selected.end();) {
            std::list<int>::iterator next = std::next(node);
            int id = (*node);
            auto* object = manager->FindObjectAs<CMapObject>(static_cast<unsigned>(id));
            if (object) {
                if (object->IsDead() == 0) {
                    Vector3 point;
                    point = object->localPosition_;
                    if (CFigureMesh* part = object->FindPartByPrefix(MarkerPartName())) {
                        Vector3 scratch;
                        const float* at = object->PartWorldPosition(&scratch.x, part);
                        lib::LoadBytes(point, at);
                        point.z = Reload(object->localPosition_.z);
                    }
                    const Quaternion* orientation = &object->localRotation_;
                    Vector3 axisY, axisX;
                    orientation->AxisY(&axisY);
                    orientation->AxisX(&axisX);
                    g_sceneRenderer.DrawSelectionMarker(&point, 0.5f, &axisX, &axisY);
                } else {
                    if (object->flags_ & MapObjectFlag::Selected) {
                        object->flags_ &= ~(MapObjectFlag::Selected | MapObjectFlag::BoxMarked);
                        // The side half's empty virtual and SetLit with 0: the marker light goes off.
                        object->attached->Unused25(0);
                        object->attached->SetLit(false);
                    }
                    selected.erase(node);
                }
            }
            node = next;
        }
    }

    g_deferredUnits.clear();
    static const FrameList kCopied[] = {FrameList::VisibleBuildings, FrameList::VisibleOther, FrameList::VisibleLevers,
                                        FrameList::VisibleUnits, FrameList::VisibleTrees, FrameList::ShadowBuildings,
                                        FrameList::ShadowOther};
    for (FrameList list : kCopied) FrameObjects(list).clear();
    FrameObjects(FrameList::ShadowLevers).clear();
    FrameObjects(FrameList::ShadowUnits).clear();
    FrameObjects(FrameList::ShadowTrees).clear();
    FrameObjects(FrameList::TouchedModels).clear();
    return 1;
}
