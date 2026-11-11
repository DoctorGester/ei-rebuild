// WorldClient's draw path: Render and DrawPass, over the whole map (the views' clip tests pick what is drawn).
// See world.h and docs/notes/world.md. Both set the renderer's render state
// (render/render_state.h) and the scene pipeline's matrix stacks (g_sceneRenderer.pipeline) in the original's
// order.
#include "game/world/object_order.h"
#include "engine/scene/camera.h"
#include "game/effects/effects.h"
#include "engine/models/figure_manager.h"
#include "game/world/frame_lists.h"
#include "game/app/singletons.h"
#include "game/terrain/ground_print.h"
#include "game/effects/lightning.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "game/net/net_session.h"
#include "engine/data/options.h"
#include "render/render.h"
#include "render/render_state.h"
#include "engine/scene/renderer_lines.h"
#include "engine/scene/sky_object.h"
#include "game/spells/spell.h"   // Light
#include "game/terrain/terrain.h"
#include "game/ui/ui.h"
#include "game/ui/ui_flying_hit_points.h"
#include "game/world/world.h"
#include "game/world/world_client_shadow_casters.h"
#include "game/effects/world_effect.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/vecmath.h"


#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>
#include "lib/rounding.h"

namespace {

// The pipeline's matrix stacks: 1 and 2 pushed by PushSceneMatrixOriginal, 5 the world draw's, 9 the views.
constexpr int kStackScene1 = 1;
constexpr int kStackScene2 = 2;
constexpr int kStackWorld = 5;
constexpr int kStackView = 9;

// SceneCamera: the scene stack's top when the world's draw pushed it, while that draw runs.
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

void VectorClear(std::vector<MapObject*>* v) { v->clear(); }

constexpr float kMapEdgeMargin = 1.0f;
constexpr const char* kMarkerPartName = "hp";

// a < b, or unordered.
bool Less(float a, float b) {
    return a < b;
}
// t when 0 < t or unordered, else 0.
// The client world's time: the tick (an int) plus the tick's fraction.
float AnimationTime() {
    const WorldClient* world = &g_clientWorld;
    return static_cast<float>(world->tick) + world->tickFraction;
}

// Sorts one draw list by the first part set's texture, then the figure, then the view depth (the game's
// comparator compares the texture and the figure by address; ours by name: object_order::DrawBefore).
// Objects with the same keys are drawn in the game's sort's order.
void SortByModel(FrameList list) {
    object_order::SortForDraw(FrameObjects(list));
}

template <typename Draw>
void DrawEach(FrameList list, Draw draw) {
    for (MapObject* object : FrameObjects(list)) draw(object);
}
// The object's place in the renderer's casters (Object3DClientSpecific::casterMark), null without a client half.
render::CasterMark* MarkOf(MapObject* object) {
    Object3DClientSpecific* half = object->ClientSpecific();
    return half ? &half->casterMark : nullptr;
}

// The drawn objects' three figure draw virtuals with the scene context; their results are ignored. The
// new renderer learns which object the meshes of a figure draw belong to (its shadow map's casters).
constexpr auto kDraw = [](MapObject* object) {
    render::BeginCaster(object, MarkOf(object), false);
    object->DrawFigure(&g_sceneRenderer);
    render::EndCaster();
};
constexpr auto kDrawUnit = [](MapObject* object) {
    render::BeginCaster(object, MarkOf(object), true);
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

const Matrix4* SceneCamera() { return g_haveSceneCamera ? &g_sceneCamera : nullptr; }

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

void WorldClient::Render() {
    render::RenderState& state = render::g_renderState;
    // The world view for the new renderer's render quality (multisampled, models sampled
    // anisotropically); the interface draws after it.
    render::BeginWorld();
    // The camera's pose this frame shows (camera::DrawnView): the view, the sky and what the draws below place
    // by the camera (camera::FrameView).
    camera::TakeFrameView(&g_camera);
    const camera::ViewPose& drawn = camera::FrameView();
    TransformPipeline* device = g_sceneRenderer.pipeline;
    UpdateLighting();
    // The sun the new renderer's shadow map is drawn from: the terrain's (Terrain::SetSun), as this
    // frame's lighting turned it. The GPU lighting's sun, ambient and point lights follow in DrawPass, once the
    // objects carrying lights are placed.
    if (terrain && terrain->sun) {
        render::ShadowSun(terrain->sun->direction);
    }
    Matrix4 view = view::ViewMatrix(drawn.position, drawn.rotation);
    PopMatrix(device->stacks[kStackView]);
    PushMatrix(device->stacks[kStackView], view.m);
    const float* top = device->Top(kStackView)->m;
    device->PushMatrix(top, kStackScene2);
    device->PushMatrix(top, kStackScene1);
    PushMatrix(device->stacks[kStackWorld], device->Top(kStackView)->m);
    g_sceneCamera = *device->Top(kStackScene1);
    g_haveSceneCamera = true;

    std::uint32_t currentFlags = flags;
    if ((currentFlags & WorldClientFlag::DrawSky) && skyDome) {
        state.alphaTest = false;
        state.stages[0].combiner = kSceneStage;
        state.depthWrite = false;
        state.cull = render::Cull::None;
        state.specular = true;
        skyDome->Draw(&g_sceneRenderer, drawn.position);
        state.cull = render::Cull::Clockwise;
        state.specular = false;
        state.depthWrite = true;
    }
    // The world's draws after the sky fog by distance (UpdateLighting set the fog), the hit points and the interface
    // don't.
    TextureDevice* renderer = g_sceneRenderer.device;
    renderer->fog = render::MeshFog::Distance;
    net::Pump();
    if ((flags & WorldClientFlag::DrawTerrain) && terrain) {
        Vector3 frame;
        camera->GroundPoint(&frame);
        terrain->ShowAllSectors();
        state.stages[0].combiner = kTerrainStage;
        terrain->DrawGround(&g_sceneRenderer);
    }
    DrawPass();
    net::Pump();
    if ((flags & WorldClientFlag::DrawTerrain) && terrain) {
        Vector3 frame;
        camera->GroundPoint(&frame);
        terrain->ShowAllSectors();
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
    PopMatrix(device->stacks[kStackWorld]);
    PopMatrix(device->stacks[kStackScene2]);
    PopMatrix(device->stacks[kStackScene1]);
    g_haveSceneCamera = false;
    renderer->fog = render::MeshFog::None;
    if (flags & WorldClientFlag::DrawNumbers) {
        state.depthTest = false;
        state.stages[0].combiner = kAlphaPassStage;
        numbers->Draw(&g_sceneRenderer);
        state.depthTest = true;
    }
    render::EndWorld();
}

int WorldClient::DrawPass() {
    render::RenderState& state = render::g_renderState;
    terrain::Terrain* currentTerrain = terrain.get();
    if (!currentTerrain || !currentTerrain->mapLoaded) {
        HandOverSceneLights(this);
        return 0;
    }
    TransformPipeline* device = g_sceneRenderer.pipeline;
    const Matrix4* view = device->Top(kStackView);
    g_deferredUnits.clear();
    static const FrameList kCleared[] = {FrameList::VisibleBuildings, FrameList::VisibleOther, FrameList::VisibleLevers,
                                         FrameList::VisibleUnits, FrameList::VisibleTrees, FrameList::ShadowBuildings,
                                         FrameList::ShadowOther, FrameList::ShadowLevers, FrameList::ShadowUnits,
                                         FrameList::ShadowTrees};
    for (FrameList list : kCleared) VectorClear(&FrameObjects(list));
    float animTime = AnimationTime();
    if (bird) bird->Update(animTime);

    // Every sector of the map's object grid (16-unit sectors up to size - 1): what is drawn is what the clip test
    // in CollectCells passes. The game took the sectors within 100 units of the camera; Rebirth's ObjectsDrawRadius
    // widened that to the whole map.
    currentTerrain = terrain.get();
    const CellRange wholeMap = {0, 0, static_cast<std::uint32_t>(lib::Truncate(terrain->width - kMapEdgeMargin)) >> 4,
                                static_cast<std::uint32_t>(lib::Truncate(terrain->height - kMapEdgeMargin)) >> 4};
    CollectCells(wholeMap, view);
    for (MapObject* object : g_deferredUnits) CollectVisible(object, view, animTime, 1, 0);
    g_deferredUnits.clear();
    for (MapObject* object : heroes) CollectVisible(object, view, animTime, 1, 1);
    // The GPU lighting's sun, ambient and point lights, the lights where the draw shows them (drawn::LightPosition):
    // an object's own light by its place just given. Before any mesh draw (a mesh's lights find their point shadows
    // by the scene's places); the ground's draws read them when the frame is drawn.
    HandOverSceneLights(this);

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

    // Light effects (three lists of the client).
    if (!bloodPrints.empty() || !footprints.empty() || !scorchMarks.empty()) {
        state.stages[0].combiner = kAlphaPassStage;
        state.alphaTest = false;
        state.depthWrite = false;
        DrawLightEffects(bloodPrints);
        DrawLightEffects(footprints);
        DrawLightEffects(scorchMarks);
        state.depthWrite = true;
    }

    // Shadows: the objects the game shadows cast in the renderer's shadow map (DrawPartsShadow draws nothing; neither
    // its CastsShadow nor its shadow sphere depends on the order).
    if (options::g_options.shadows) {
        state.stages[0].combiner = kAlphaPassStage;
        state.alphaTest = false;
        state.depthWrite = true;
        DrawEach(FrameList::ShadowLevers, kDrawShadow);
        DrawEach(FrameList::ShadowOther, kDrawShadow);
        DrawEach(FrameList::ShadowBuildings, kDrawShadow);
        DrawEach(FrameList::ShadowTrees, kDrawShadow);
        DrawEach(FrameList::ShadowUnits, kDrawShadow);
    }

    // Selection markers (selectedIds of object ids) in single-player mode 1 with units drawn: a marker at
    // each living object (at its named part when it has one); dead ones leave the list.
    std::vector<int>& selected = selectedIds;
    if (!selected.empty() && options::g_options.selectionType == 1 && (flags & WorldClientFlag::DrawUnits)) {
        state.alphaTest = false;
        g_sceneRenderer.SetTexture(0, nullptr);
        MapObjectManager* manager = objectManager.get();
        // In order: a living object's marker drawn, a dead one unselected and its id erased.
        std::erase_if(selected, [manager](int objectId) {
            auto* object = manager->FindObjectAs<MapObject>(static_cast<unsigned>(objectId));
            if (!object) return false;
            if (object->IsDead() == 0) {
                Vector3 point;
                point = object->localPosition;
                if (FigureMesh* part = object->FindPartByPrefix(kMarkerPartName)) {
                    point = object->PartWorldPosition(part);
                    point.z = object->localPosition.z;
                }
                const Quaternion* orientation = &object->localRotation;
                const Vector3 axisY = orientation->AxisY();
                const Vector3 axisX = orientation->AxisX();
                g_sceneRenderer.DrawSelectionMarker(point, 0.5f, axisX, axisY);
                return false;
            }
            if (object->flags & MapObjectFlag::Selected) {
                object->flags &= ~(MapObjectFlag::Selected | MapObjectFlag::BoxMarked);
                // SetLit with 0: the marker light goes off.
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
    return 1;
}
