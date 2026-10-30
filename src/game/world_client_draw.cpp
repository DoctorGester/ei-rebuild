// WorldClient's draw path: Render and DrawPass, with Rebirth's LandscapeDrawRadius and ObjectsDrawRadius
// behind their flags. See world.h and docs/notes/world.md. Both set the renderer's render state
// (render/render_state.h) and the scene pipeline's matrix stacks (g_sceneRenderer.pipeline) in the original's
// order.
#include "address_order.h"
#include "camera.h"
#include "effects.h"
#include "figure_manager.h"
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
#include "spell.h"   // Light
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
#include "platform/clock.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"

#include "ei/rebirth.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

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
    std::uint32_t value = 100;
    std::uint32_t data = 0, size = sizeof(data), type = 0;
    if (settings_file::ReadValue("Software\\Gipat.Ru\\EI_Starter\\Settings", name, &type, &data, &size) == 0 && type == settings_file::kTypeDword)
        value = data;
    float radius = static_cast<float>(static_cast<double>(value));
    return radius;
}

float LandscapeDrawRadius() {
    static const float setting = StarterDrawRadius("LandscapeDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::LandscapeDrawRadius) ? setting : 100.0f;
}

void VectorClear(std::vector<MapObject*>* v) { v->clear(); }

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
int LightEffects1B8() { return LightEffectsSetting(options::g_options.enableBloodprints); }
int LightEffects1BC() { return LightEffectsSetting(options::g_options.enableFootprints); }
int LightEffects1C0() { return LightEffectsSetting(options::g_options.enableFireprints); }
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
int Ftol(float v) { return static_cast<int>(static_cast<std::int64_t>(v)); }   // the low half; 0 for NaN
// The client world's time: the tick (an int) plus the tick's fraction, rounded once.
float AnimationTime() {
    const WorldClient* world = &g_clientWorld;
    return pc24::Add(static_cast<double>(world->tick), world->tickFraction);
}

// Sorts one draw list by the first part set's texture, then the figure, then the view depth (the game's
// comparator compares the texture and the figure by address; ours by name: address_order::DrawBefore).
// Objects with the same keys are drawn in the game's sort's order.
void SortByModel(FrameList list) {
    std::vector<MapObject*>& v = FrameObjects(list);
    lib::IntroSort(v.data(), v.data() + v.size(),
                   [](MapObject* a, MapObject* b) { return address_order::DrawBefore(a, b); });
}

template <typename Draw>
void DrawEach(FrameList list, Draw draw) {
    for (MapObject* object : FrameObjects(list)) draw(object);
}
// The drawn objects' three figure draw virtuals with the scene context; their results are ignored. The
// new renderer learns which object the meshes of a figure draw belong to (its shadow map's casters).
constexpr auto kDraw = [](MapObject* object) {
    render::BeginCaster(object, false);
    object->DrawFigure(&g_sceneRenderer);
    render::EndCaster();
};
constexpr auto kDrawUnit = [](MapObject* object) {
    render::BeginCaster(object, true);
    object->DrawCollectDirty(&g_sceneRenderer);
    render::EndCaster();
};
constexpr auto kDrawShadow = [](MapObject* object) { object->DrawFigureShadow(&g_sceneRenderer); };

// Draws every print of a ground print vector.
template <typename T>
void DrawLightEffects(std::vector<T>& prints) {
    for (GroundPrint& print : prints) print.Draw();
}

} // namespace

const Matrix4* worlddata::SceneCamera() { return g_haveSceneCamera ? &g_sceneCamera : nullptr; }

int QueueScreenEntry(MapObject* object, std::vector<ScreenEntry>* queued) {
    if (!(object->flags & MapObjectFlag::Shown) || (object->flags & MapObjectFlag::NotDrawn)) return 0;
    const Object3D* figure = object;
    ScreenEntry entry;
    entry.classId = object->GetClassId();
    entry.objectId = static_cast<unsigned>(object->id);
    entry.rect = figure->dirtyRect;
    queued->push_back(entry);
    return 1;
}

int WorldClient::Render() {
    render::RenderState& state = render::g_renderState;
    // The world view for the new renderer's render quality (multisampled, models sampled
    // anisotropically); the interface draws after it.
    render::BeginWorld();
    // The camera's pose this frame shows (camera::DrawnView): the view, the sky and what the draws below place
    // by the camera (camera::FrameView).
    camera::TakeFrameView(&g_camera);
    const camera::ViewPose& drawn = camera::FrameView();
    TextureDevice* renderer = g_sceneRenderer.device;
    TransformPipeline* device = g_sceneRenderer.pipeline;
    UpdateLighting();
    // The sun the new renderer's shadow map is drawn from: the terrain's (Terrain::SetSun), as this
    // frame's lighting turned it; the GPU lighting's sun, ambient and point lights.
    if (terrain && terrain->sun) {
        const Vector3& d = terrain->sun->direction;
        const float direction[3] = {d.x, d.y, d.z};
        render::ShadowSun(direction);
    }
    HandOverSceneLights(this);
    Matrix4 view;
    view::ViewMatrix(view.m, drawn.position, drawn.rotation);
    PopMatrix(device->stacks[kStackView]);
    PushMatrix(device->stacks[kStackView], view.m);
    const float* top = device->Top(kStackView)->m;
    device->PushMatrix(top, kStackScene2);
    device->PushMatrix(top, kStackScene1);
    PushMatrix(device->stacks[kStackWorld], device->Top(kStackView)->m);
    g_sceneCamera = *device->Top(kStackScene1);
    g_haveSceneCamera = true;

    std::uint32_t currentFlags = flags;
    void* skyColour = skyDome;
    if ((currentFlags & WorldClientFlag::DrawSky) && skyColour) {
        state.alphaTest = false;
        state.stages[0].combiner = kSceneStage;
        state.depthWrite = false;
        state.cull = render::Cull::None;
        state.specular = true;
        renderer->distanceFog = 0;
        skyDome->Draw(&g_sceneRenderer, drawn.position);
        renderer->distanceFog = 1;
        state.cull = render::Cull::Clockwise;
        state.specular = false;
        state.depthWrite = true;
    }
    net::Pump();
    if ((flags & WorldClientFlag::DrawTerrain) && terrain) {
        Vector3 frame;
        camera->GroundPoint(&frame);
        terrain->SetVisibleSectors(camera->localPosition.x, camera->localPosition.y, LandscapeDrawRadius());
        state.stages[0].combiner = kTerrainStage;
        terrain->DrawGround(&g_sceneRenderer);
    }
    DrawPass();
    net::Pump();
    if ((flags & WorldClientFlag::DrawTerrain) && terrain) {
        Vector3 frame;
        camera->GroundPoint(&frame);
        terrain->SetVisibleSectors(camera->localPosition.x, camera->localPosition.y, LandscapeDrawRadius());
        state.alphaTest = false;
        state.stages[0].combiner = kSceneStage;
        terrain->DrawWater(&g_sceneRenderer);
    }
    // The shadow map's casters the game did not draw (the map is fitted to the ground drawn above),
    // and the ground's own.
    if (flags & WorldClientFlag::DrawTerrain) HandOverTerrainCasters(this);
    HandOverOutsideCasters(this);
    net::Pump();
    if (flags & WorldClientFlag::DrawEffects) {
        state.stages[0].combiner = kAlphaPassStage;
        state.blend = true;
        state.alphaTest = false;
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = false;
        arrayManager->Draw(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = false;
        ParticleManager* particles = ClientParticleManager();
        particles->DrawAll(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        listManager->DrawAll(&g_sceneRenderer);
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        rainSnow.Draw(&g_sceneRenderer);
        state.depthWrite = true;
    }
    state.stages[0].combiner = kSceneStage;
    net::Pump();
    DrawLineLists();
    PopMatrix(device->stacks[kStackWorld]);
    PopMatrix(device->stacks[kStackScene2]);
    PopMatrix(device->stacks[kStackScene1]);
    g_haveSceneCamera = false;
    if (flags & WorldClientFlag::DrawNumbers) {
        state.depthTest = false;
        state.stages[0].combiner = kAlphaPassStage;
        renderer->distanceFog = 0;
        numbers->Draw(&g_sceneRenderer);
        renderer->distanceFog = 1;
        state.depthTest = true;
    }
    render::EndWorld();
    return 1;
}

int WorldClient::DrawLineLists() {
    for (auto& node : lineLists) node.second->Draw(g_sceneRenderer.pipeline, g_sceneRenderer.device);
    return 1;
}

int WorldClient::DrawPass() {
    render::RenderState& state = render::g_renderState;
    terrain::Terrain* currentTerrain = terrain;
    if (!currentTerrain || !currentTerrain->mapLoaded) return 0;
    TransformPipeline* device = g_sceneRenderer.pipeline;
    std::uint32_t now = platform::Milliseconds();
    const Matrix4* view = device->Top(kStackView);
    g_deferredUnits.clear();
    static const FrameList kCleared[] = {FrameList::VisibleBuildings, FrameList::VisibleOther, FrameList::VisibleLevers,
                                         FrameList::VisibleUnits, FrameList::VisibleTrees, FrameList::ShadowBuildings,
                                         FrameList::ShadowOther, FrameList::ShadowLevers, FrameList::ShadowUnits,
                                         FrameList::ShadowTrees, FrameList::TouchedModels};
    for (FrameList list : kCleared) VectorClear(&FrameObjects(list));
    float animTime = AnimationTime();
    const float* position = &CameraAnchor(camera)->x;
    if (bird) bird->Update(animTime);

    // The sectors around the camera: the position widened by the margin, clamped to the map
    // (size - 1) and to 0, in 16-unit sectors.
    const float margin = ObjectsDrawRadius();
    const float edge = kMapEdgeMargin;
    currentTerrain = terrain;
    float yHi = MinKeepB(AddRounded(position[1], margin), SubRounded(currentTerrain->height, edge));
    float xMax = SubRounded(terrain->width, edge);
    float xHi = MinKeepB(AddRounded(position[0], margin), xMax);
    float yMax = SubRounded(terrain->height, edge);
    float yLo = MaxKeepA(0.0f, Reload(MinKeepB(SubRounded(position[1], margin), yMax)));
    float xMax2 = SubRounded(terrain->width, edge);
    float xLo = MaxKeepA(0.0f, Reload(MinKeepB(SubRounded(position[0], margin), xMax2)));
    std::uint32_t sx0 = static_cast<std::uint32_t>(Ftol(xLo)) >> 4;
    rangeFirstX = sx0;
    rangeX = sx0;
    std::uint32_t sy0 = static_cast<std::uint32_t>(Ftol(yLo)) >> 4;
    rangeFirstY = sy0;
    rangeY = sy0;
    rangeLastX = static_cast<std::uint32_t>(Ftol(xHi)) >> 4;
    rangeLastY = static_cast<std::uint32_t>(Ftol(yHi)) >> 4;

    int touched = 0;
    auto* sector = &objectGrid.rows[sy0][sx0];
    while (sector) {
        for (MapObject* object : *sector) touched += CollectVisible(object, view, animTime, now, 0, 0);
        std::uint32_t x = ++rangeX;
        if (x > rangeLastX) {
            rangeX = rangeFirstX;
            std::uint32_t y = ++rangeY;
            if (y > rangeLastY) break;
        }
        sector = &objectGrid.rows[rangeY][rangeX];
    }
    for (MapObject* object : g_deferredUnits) touched += CollectVisible(object, view, animTime, now, 1, 0);
    g_deferredUnits.clear();
    for (MapObject* object : heroes) touched += CollectVisible(object, view, animTime, now, 1, 1);

    // Models in view stay loaded: the most recently used max(10, n / 8) lose MapObjectFlag::Static.
    std::vector<MapObject*>& models = FrameObjects(FrameList::TouchedModels);
    if (models.begin() != models.end()) {
        // The most recently used first; the models used in the same frame stay in the game's sort's order.
        lib::IntroSort(models.data(), models.data() + models.size(), [](MapObject* a, MapObject* b) {
            return b->ClientSpecific()->touchTime < a->ClientSpecific()->touchTime;
        });
        int count = touched >> 3;
        if (count < 10) count = 10;
        for (MapObject** p = models.data(); p != models.data() + models.size() && count != 0; --count) {
            MapObject* object = *p++;
            object->flags &= ~MapObjectFlag::Static;
        }
    }

    // The frame's window rectangles.
    std::vector<ScreenEntry>& queued = screenEntries;
    if (flags & (WorldClientFlag::DrawObjects | WorldClientFlag::DrawUnits)) {
        state.stages[0].combiner = kAlphaPassStage;
        queued.clear();
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.blend = true;
        state.alphaTest = true;
    }
    if (flags & WorldClientFlag::DrawObjects) {
        SortByModel(FrameList::VisibleBuildings);
        SortByModel(FrameList::VisibleOther);
        SortByModel(FrameList::VisibleLevers);
        SortByModel(FrameList::VisibleTrees);
        state.alphaReference = 0xC8;
        DrawEach(FrameList::VisibleOther, kDraw);
        DrawEach(FrameList::VisibleLevers, kDrawUnit);
        MapObject** end = FrameObjects(FrameList::VisibleLevers).data() + FrameObjects(FrameList::VisibleLevers).size();
        for (MapObject** p = FrameObjects(FrameList::VisibleLevers).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
        DrawEach(FrameList::VisibleBuildings, kDraw);
        DrawEach(FrameList::VisibleTrees, kDraw);
    }
    if ((flags & WorldClientFlag::DrawUnits) && FrameObjects(FrameList::VisibleUnits).begin() != FrameObjects(FrameList::VisibleUnits).end()) {
        SortByModel(FrameList::VisibleUnits);
        state.alphaReference = 10;
        state.stages[0].combiner = kAlphaPassStage;
        DrawEach(FrameList::VisibleUnits, kDrawUnit);
        MapObject** end = FrameObjects(FrameList::VisibleUnits).data() + FrameObjects(FrameList::VisibleUnits).size();
        for (MapObject** p = FrameObjects(FrameList::VisibleUnits).data(); p != end; ++p) QueueScreenEntry(*p, &queued);
    }

    // Light effects (three lists of the client, each behind its setting).
    if (!bloodPrints.empty() || !footprints.empty() || !scorchMarks.empty()) {
        state.stages[0].combiner = kAlphaPassStage;
        state.alphaTest = false;
        state.depthWrite = false;
        if (LightEffects1B8()) DrawLightEffects(bloodPrints);
        if (LightEffects1BC()) DrawLightEffects(footprints);
        if (LightEffects1C0()) DrawLightEffects(scorchMarks);
        state.depthWrite = true;
    }

    // Shadows: projected along the camera's Z axis (its rotation) scaled by a constant.
    if (flags & WorldClientFlag::DrawShadows) {
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
        camera::FrameView().rotation.AxisZ(&sun);
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
        PopMatrix(device->stacks[kStackScene2]);
        PopMatrix(device->stacks[kStackScene1]);
    }

    // Selection markers (selectedIds of object ids) in single-player mode 1 with units drawn: a marker at
    // each living object (at its named part when it has one); dead ones leave the list.
    std::vector<int>& selected = selectedIds;
    if (!selected.empty() && options::g_options.selectionType == 1 && (flags & WorldClientFlag::DrawUnits)) {
        state.alphaTest = false;
        g_sceneRenderer.SetTexture(0, nullptr);
        MapObjectManager* manager = objectManager;
        // In order: a living object's marker drawn, a dead one unselected and its id erased.
        std::erase_if(selected, [manager](int objectId) {
            auto* object = manager->FindObjectAs<MapObject>(static_cast<unsigned>(objectId));
            if (!object) return false;
            if (object->IsDead() == 0) {
                Vector3 point;
                point = object->localPosition;
                if (FigureMesh* part = object->FindPartByPrefix(MarkerPartName())) {
                    Vector3 scratch;
                    const float* at = object->PartWorldPosition(&scratch.x, part);
                    lib::LoadBytes(point, at);
                    point.z = Reload(object->localPosition.z);
                }
                const Quaternion* orientation = &object->localRotation;
                Vector3 axisY, axisX;
                orientation->AxisY(&axisY);
                orientation->AxisX(&axisX);
                g_sceneRenderer.DrawSelectionMarker(point, 0.5f, axisX, axisY);
                return false;
            }
            if (object->flags & MapObjectFlag::Selected) {
                object->flags &= ~(MapObjectFlag::Selected | MapObjectFlag::BoxMarked);
                // The side half's empty virtual and SetLit with 0: the marker light goes off.
                object->attached->Unused25(0);
                object->attached->SetLit(false);
            }
            return true;
        });
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
