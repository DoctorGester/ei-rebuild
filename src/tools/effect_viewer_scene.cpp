// The effect viewer's world (effect_viewer_scene.h): its managers on the install's archives, the ground, the figure,
// the free camera and the frame's draw. docs/notes/effects.md, "The effect viewer".
#include "tools/effect_viewer_scene.h"

#include "engine/core/base_manager.h"
#include "engine/core/engine_globals.h"
#include "engine/data/resource.h"
#include "engine/data/resource_manager.h"
#include "engine/models/animation.h"
#include "engine/models/animation_db.h"
#include "engine/models/figure_instance.h"
#include "engine/models/figure_manager.h"
#include "engine/models/link_figure.h"
#include "engine/models/link_loader.h"
#include "engine/models/material.h"
#include "engine/models/texture.h"
#include "engine/particles/particle_record.h"
#include "engine/particles/particle_source.h"
#include "engine/scene/device.h"
#include "engine/scene/light.h"
#include "engine/scene/pipeline.h"
#include "engine/scene/scene_renderer.h"

#include "lib/maths.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/render_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace tools::effect_viewer {

ViewerScene g_scene;

namespace {

constexpr std::uint32_t kTickMs = 55;          // the game's tick (app::g_updatePeriod)
constexpr int kLongestCatchUp = 5;             // ticks run at most in one frame
constexpr float kRightAngle = kPi * 0.5f;
constexpr float kLeastPitch = 0.05f;
constexpr float kMostPitch = 1.5f;
constexpr float kNearest = 2.0f;
constexpr float kFarthest = 150.0f;
constexpr float kTurnPerPixel = 0.008f;
constexpr float kMovePerSecond = 12.0f;        // the focus's speed with the keys, at a distance of 12
constexpr float kFigureScale = 0.5f;           // the morph the heroes are built at (the figure_alone check's)
constexpr const char* kSkinTexture = "viewer_skin";   // the figure's composed texture
constexpr int kSkinSide = 256;                 // the skins' size (a dressed unit's main texture's)
constexpr unsigned kSkinMips = 3;

// The heroes' figure parts in the game's order (game/objects/object3d.h, kHeroFigureParts), the hair the first one.
constexpr std::array<const char*, 16> kHeroParts = {"hd",  "bd",  "hp",  "hr.01", "lh1", "lh2", "lh3", "rh1",
                                                     "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"};

// The game's points (FigurePoint::Kind::Game) by AttachPoint, what each is to the game.
constexpr std::array<const char*, 13> kGamePoints = {
    "the casting hand (rh3: a hero's effect point)", "head (hd)", "above the head (hd, 1 higher)", "left hand (lh2)",
    "right hand (rh2)", "left leg (ll2)", "right leg (rl2)", "body (bd)", "body00", "body01", "body02", "body03",
    "body04"};
constexpr int kFirstBodyPoint = 8;   // body00 ... body04: listed only when the figure has such a part

// The points the game reads by bone that no AttachPoint names: a unit's feet (CarrierState's) and a creature's
// weapon point without a weapon (UnitClientSpecific::WeaponPoint's hand node).
struct NamedGamePoint {
    const char* name = nullptr;
    const char* bone = nullptr;
    const char* label = nullptr;
};
constexpr std::array<NamedGamePoint, 3> kNamedGamePoints = {{
    {"left_foot", "ll3", "the left foot the effects read (ll3)"},
    {"right_foot", "rl3", "the right foot the effects read (rl3)"},
    {"weapon", "baserh3", "a creature's weapon point (baserh3)"},
}};

// The ground: tiles of kTile units over a square of kTiles a side round the origin, in two greys.
constexpr int kTiles = 48;
constexpr float kTile = 1.0f;
constexpr std::uint32_t kLightTile = 0xFF5A5E66u;
constexpr std::uint32_t kDarkTile = 0xFF41444Bu;

// The scene renderer's stacks the world's draw uses (world_client_draw.cpp's).
constexpr int kStackScene1 = 1;
constexpr int kStackScene2 = 2;
constexpr int kStackWorld = 5;
constexpr int kStackView = 9;

// The figure parts' drawers: FigureManager::CreateObject gives each part one (the game's ClientSpecificManager makes
// them with its other halves).
class PartDrawers final : public BaseManager {
public:
    EngineObject* NewObject(int objectType) override {
        return static_cast<SideObjectType>(objectType) == SideObjectType::Drawer ? new FigureMeshDrawer() : nullptr;
    }
    const char* ClassName() override { return "PartDrawers"; }
    ClassId GetClassId() override { return ClassId::PartDrawers; }
};

// database.res for its animation databases (MapObjectManager's archive in the game).
class DatabaseArchive final : public ResourceManager {
public:
    EngineObject* NewObject(int) override { return nullptr; }
    const char* ClassName() override { return "DatabaseArchive"; }
    ClassId GetClassId() override { return ClassId::ViewerDatabase; }
};

void DeleteLoader(LinkFigureLoader* loader) {
    if (loader) loader->Release();
}

bool StartsWith(std::string_view text, std::string_view start) {
    return text.size() >= start.size() && text::CompareIgnoringCase(text.substr(0, start.size()), start) == 0;
}

std::string_view EntryName(const FigureAnimation& entry) {
    return std::string_view(entry.name, strnlen(entry.name, sizeof(entry.name)));
}

} // namespace

// ---- The camera ------------------------------------------------------------------------------------------------------

Quaternion FreeCamera::Rotation() const {
    Quaternion rotation = Quaternion::AxisAngle({0.0f, 0.0f, 1.0f}, heading);
    const Vector3 right = rotation.AxisX();
    rotation.PreMultiply(Quaternion::AxisAngle(right, -(kRightAngle + pitch)));
    return rotation;
}

Vector3 FreeCamera::Position() const { return focus - Rotation().AxisZ() * distance; }

// ---- The scene -------------------------------------------------------------------------------------------------------

ViewerScene::ViewerScene() : ManagerContext(ContextKind::Party), loader(nullptr, &DeleteLoader) {}

ViewerScene::~ViewerScene() = default;

void ViewerScene::Destroy() { Stop(); }

const char* ViewerScene::ClassName() { return "ViewerScene"; }

ClassId ViewerScene::GetClassId() { return ClassId::ViewerScene; }

FigureManager* ViewerScene::GetFigureManager() { return figures.get(); }
BaseManager* ViewerScene::SpecificManager() { return drawers.get(); }
LinkFigureLoader* ViewerScene::FigureLoader() { return loader.get(); }
TextureManager* ViewerScene::GetTextureManager() { return textures.get(); }
MaterialManager* ViewerScene::GetMaterialManager() { return materials.get(); }
LightFactory* ViewerScene::GetLightFactory() { return lights.get(); }
float ViewerScene::SceneTime() { return static_cast<float>(tick) + tickFraction; }

bool ViewerScene::Start() {
    g_sceneRenderer.world = this;
    textures = std::make_unique<TextureManager>();
    textures->Init(this);
    if (!textures->context) {
        ei::Log("ei-effects: the install's texture archives (%stextures.res) did not open", g_textFolder);
        return false;
    }
    textures->SetDevice(g_sceneRenderer.device);
    materials = std::make_unique<MaterialManager>();
    materials->Init(this);
    lights = std::make_unique<LightFactory>();
    lights->Init(this);
    drawers = std::make_unique<PartDrawers>();
    drawers->Init(this);
    figures = std::make_unique<FigureManager>();
    figures->Init(this);
    database = std::make_unique<DatabaseArchive>();
    database->Init(this);
    database->AddArchive(g_textFolder + "database.res");
    LinkFigureLoader* made = new LinkFigureLoader(g_sceneRenderer.device, g_textFolder);
    made->AddRef();
    loader.reset(made);
    particlehost::Use(host);
    particles = std::make_unique<ParticleManager>();
    particles->Init(this);
    g_sceneRenderer.AddDefaultLight();
    g_sceneRenderer.SetFogColour(0.16f, 0.18f, 0.22f);
    return true;
}

void ViewerScene::Stop() {
    if (figure) {
        host.LetGoAll(*figure);
        figure->Release();
        figure = nullptr;
    }
    animations.reset();
    if (particles) particles->Destroy();
    particles.reset();
    g_sceneRenderer.SetSun(nullptr);
    for (BaseManager* manager : std::initializer_list<BaseManager*>{figures.get(), drawers.get(), lights.get(),
                                                                     materials.get(), textures.get(), database.get()})
        if (manager) manager->Destroy();
    loader.reset();
    figures.reset();
    drawers.reset();
    lights.reset();
    materials.reset();
    textures.reset();
    database.reset();
    if (g_sceneRenderer.world == this) g_sceneRenderer.world = nullptr;
}

// ---- The figure ------------------------------------------------------------------------------------------------------

bool ViewerScene::BuildFigure(const std::string& name) {
    if (figure) {
        host.LetGoAll(*figure);
        figure->Release();
        figure = nullptr;
    }
    animations.reset();
    animation = -1;
    auto* made = figures->CreateObjectAs<FigureInstance>(FigureObjectType::Instance, kNoObjectId);
    if (!made) return false;
    made->AddRef();
    std::vector<std::string> parts;
    if (name == "unhuma" || name == "unhufe") parts.assign(kHeroParts.begin(), kHeroParts.end());
    if (!made->Build(name, {kFigureScale, kFigureScale, kFigureScale}, parts)) {
        ei::Log("ei-effects: no figure %s in the install", name);
        made->Release();
        return false;
    }
    // Its bare skin: a texture composed of the one layer "<model>skin_00.mmp" (redress.res), as the game composes a
    // dressed unit's of its skin and its items (unit_skins.cpp, DressUnit).
    SkinRequest skin;
    skin.textures.push_back(name + "skin_00.mmp");
    skin.side = kSkinSide;
    std::string dress;
    if (!textures->Compose(&dress, &skin, kSkinTexture, kSkinTexture, kSkinMips, MmpFormat::A1R5G5B5))
        ei::Log("ei-effects: no skin %s for the figure", skin.textures[0]);
    made->Dress(kSkinTexture);
    figure = made;
    model = name;
    const RecordFile record = database->FindRecord(name + ".adb");
    if (record.Any()) animations = ReadAnimationDatabase(record);
    figure->animations = animations.get();
    PlaceFigure();
    const int standing = FindAnimation({"idle", "stand"});
    PlayAnimation(standing >= 0 ? standing : 0);
    ei::Log("ei-effects: the figure %s: %d parts, %d animations", name, static_cast<int>(figure->parts.size()),
            animations ? static_cast<int>(animations->animations.size()) : 0);
    return true;
}

void ViewerScene::PlayAnimation(int index) {
    if (!figure || !animations || index < 0 || index >= static_cast<int>(animations->animations.size())) return;
    animation = index;
    figure->PlayAnimation(&animations->animations[static_cast<std::size_t>(index)]);
    figure->animation->UpdatePose();
}

int ViewerScene::FindAnimation(std::initializer_list<std::string_view> words) const {
    if (!animations) return -1;
    for (const std::string_view word : words)
        for (std::size_t i = 0; i < animations->animations.size(); ++i) {
            std::string entry(EntryName(animations->animations[i]));
            text::LowerInPlace(entry.data(), entry.size());
            if (entry.find(word) != std::string::npos) return static_cast<int>(i);
        }
    return -1;
}

void ViewerScene::SetGait(bool walk, bool run) {
    walking = walk;
    host.running = run;
    const int gait = !walk ? FindAnimation({"idle", "stand"}) : run ? FindAnimation({"run", "walk"}) : FindAnimation({"walk"});
    if (gait >= 0) PlayAnimation(gait);
    PlaceFigure();
}

std::vector<std::string> ViewerScene::AnimationNames() const {
    std::vector<std::string> names;
    if (animations)
        for (const FigureAnimation& entry : animations->animations) names.emplace_back(EntryName(entry));
    return names;
}

std::vector<FigurePoint> ViewerScene::Points() const {
    using Kind = FigurePoint::Kind;
    using Group = FigurePoint::Group;
    std::vector<FigurePoint> points;
    points.push_back({Kind::File, Group::Effect, {}, {}, "file", "the file's part"});
    points.push_back({Kind::Origin, Group::Effect, {}, {}, "origin", "the figure's origin"});
    if (!figure) return points;
    for (int i = 0; i < static_cast<int>(kGamePoints.size()); ++i) {
        const auto point = static_cast<AttachPoint>(i);
        if (i >= kFirstBodyPoint && !figure->FindPartByPrefix(AttachPointPart(point))) continue;
        const std::string name(effectdata::PartName(i));
        points.push_back({Kind::Game, Group::Game, point, {}, name, name + ": " + kGamePoints[static_cast<std::size_t>(i)]});
    }
    FigureAnimationState* skeleton = figure->animation.Get();
    for (const NamedGamePoint& named : kNamedGamePoints)
        if (skeleton && skeleton->FindBone(named.bone))
            points.push_back({Kind::Named, Group::Game, {}, named.bone, named.name, std::string(named.name) + ": " + named.label});
    if (skeleton) {
        for (const FigureBone* bone : skeleton->bones)
            if (bone->mesh) points.push_back({Kind::Named, Group::Parts, {}, bone->name, bone->name, bone->name});
        for (const FigureBone* bone : skeleton->bones)
            if (!bone->mesh) points.push_back({Kind::Named, Group::Bones, {}, bone->name, bone->name, bone->name});
    }
    for (const FigureMesh* part : figure->parts)
        if (!skeleton || !skeleton->FindBone(part->name))
            points.push_back({Kind::Named, Group::Bones, {}, part->name, part->name, part->name + " (a part, no bone)"});
    return points;
}

std::optional<FigurePoint> ViewerScene::FindPoint(std::string_view name) const {
    for (FigurePoint& point : Points())
        if (text::CompareIgnoringCase(point.name, name) == 0) return std::move(point);
    return std::nullopt;
}

void ViewerScene::PlaceFigure() {
    if (!figure) return;
    if (!walking) {
        figure->localPosition = standAt;
        return;
    }
    const float radius = std::max(circleRadius, 0.5f);
    const Vector3 centre = standAt;
    figure->localPosition = centre + Vector3{lib::Cos(walkAngle) * radius, lib::Sin(walkAngle) * radius, 0.0f};
    // Facing along the circle (counter-clockwise): the figure's front is its -y (the game's units').
    figure->localRotation = Quaternion::AxisAngle({0.0f, 0.0f, 1.0f}, walkAngle + kPi);
}

// ---- The clock -------------------------------------------------------------------------------------------------------

void ViewerScene::Advance(std::uint32_t now, void (*ticked)()) {
    if (!clockStarted) {
        clockStarted = true;
        lastTick = lastFrame = now;
    }
    int ran = 0;
    while (now - lastTick >= kTickMs && ran < kLongestCatchUp) {
        lastTick += kTickMs;
        ++ran;
        if (paused) continue;   // the viewer's own pause is its own (Viewer::playing)
        ++tick;
        if (figure && figure->animation && animations && animation >= 0) {
            FigureAnimationState& state = *figure->animation;
            const FigureAnimation& playing = animations->animations[static_cast<std::size_t>(animation)];
            if (state.time + 1.0f >= static_cast<float>(playing.frameCount)) PlayAnimation(animation);
            else state.Advance(1.0f);
            state.UpdatePose();
        }
        if (walking) walkAngle = std::fmod(walkAngle + Stride() / std::max(circleRadius, 0.5f), kPi + kPi);
        PlaceFigure();
        if (particles) particles->TickSources(0);
        if (ticked) ticked();
    }
    if (now - lastTick >= kTickMs) lastTick = now;   // far behind (a stall): no catching up beyond kLongestCatchUp
    tickFraction = paused ? 0.0f : static_cast<float>(now - lastTick) / static_cast<float>(kTickMs);
    host.tickFraction = tickFraction;
    // The camera's keys move its focus on the frame's time.
    const float seconds = std::min(static_cast<float>(now - lastFrame) / 1000.0f, 0.1f);
    lastFrame = now;
    if (keysHeld) {
        const Quaternion rotation = camera.Rotation();
        Vector3 forward = rotation.AxisZ();
        forward.z = 0.0f;
        Vector3 right = rotation.AxisX();
        right.z = 0.0f;
        if (LengthSquared(forward) > 0.0f) forward = forward * (1.0f / Length(forward));
        if (LengthSquared(right) > 0.0f) right = right * (1.0f / Length(right));
        Vector3 move;
        if (keysHeld & 1) move += forward;
        if (keysHeld & 2) move -= forward;
        if (keysHeld & 4) move -= right;
        if (keysHeld & 8) move += right;
        camera.focus += move * (kMovePerSecond * seconds * camera.distance / 12.0f);
    }
}

// ---- The draw --------------------------------------------------------------------------------------------------------

void ViewerScene::DrawGround() {
    render::RenderState& state = render::g_renderState;
    state.stages[0].combiner = kAlphaPassStage;
    state.alphaTest = false;
    state.blend = true;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.depthWrite = true;
    state.cull = render::Cull::None;
    g_sceneRenderer.SetTexture(0, nullptr);
    static std::vector<VertexPacket> blocks;
    static std::vector<render::TexCoord> uvs;
    static std::vector<std::uint16_t> indices;
    const float origin = -kTile * static_cast<float>(kTiles) * 0.5f;
    for (int shade = 0; shade < 2; ++shade) {
        blocks.clear();
        indices.clear();
        for (int y = 0; y < kTiles; ++y)
            for (int x = 0; x < kTiles; ++x) {
                if (((x + y) & 1) != shade) continue;
                const float left = origin + static_cast<float>(x) * kTile, bottom = origin + static_cast<float>(y) * kTile;
                VertexPacket block{};
                const float xs[4] = {left, left + kTile, left + kTile, left};
                const float ys[4] = {bottom, bottom, bottom + kTile, bottom + kTile};
                for (int k = 0; k < 4; ++k) {
                    block.block[0][k] = xs[k];
                    block.block[1][k] = ys[k];
                    block.block[2][k] = host.groundHeight;
                    block.block[3][k] = 1.0f;
                }
                const auto first = static_cast<std::uint16_t>(blocks.size() * 4);
                for (const int corner : {0, 2, 1, 0, 3, 2}) indices.push_back(static_cast<std::uint16_t>(first + corner));
                blocks.push_back(block);
            }
        uvs.assign(blocks.size() * 4, render::TexCoord{});
        g_sceneRenderer.device->DrawFlatBlocks(blocks.data(), uvs.data(), static_cast<int>(blocks.size()), indices.data(),
                                               static_cast<int>(indices.size()), shade ? kDarkTile : kLightTile,
                                               0xFF000000u, false);
    }
    state.cull = render::Cull::Clockwise;
}

void ViewerScene::Draw() {
    render::RenderState& state = render::g_renderState;
    render::BeginWorld();
    const Quaternion rotation = camera.Rotation();
    const Vector3 position = camera.Position();
    g_sceneRenderer.view.position = position;
    g_sceneRenderer.view.rotation = rotation;
    TransformPipeline* pipeline = g_sceneRenderer.pipeline;
    const Matrix4 view = view::ViewMatrix(position, rotation);
    pipeline->PopMatrix(kStackView);
    pipeline->PushMatrix(view.m, kStackView);
    const float* top = pipeline->Top(kStackView)->m;
    pipeline->PushMatrix(top, kStackScene2);
    pipeline->PushMatrix(top, kStackScene1);
    pipeline->PushMatrix(pipeline->Top(kStackView)->m, kStackWorld);

    // The light: the default sun and a grey ambient, for the figure and the ground (the GPU lighting's).
    Light* sun = g_sceneRenderer.sun;
    if (sun) g_sceneRenderer.SetSunAndAmbient(sun, 0.45f, 0.45f, 0.45f);
    render::SceneLights lighting{};
    if (sun) {
        for (int k = 0; k < 3; ++k) lighting.sunColour[k] = lighting.daySunColour[k] = sun->color[k];
        lighting.sunDirection = sun->direction;
    }
    for (int k = 0; k < 3; ++k) lighting.ambientColour[k] = lighting.dayAmbientColour[k] = 0.45f;
    lighting.camera = position;
    lighting.clientTicks = static_cast<double>(tick) + static_cast<double>(tickFraction);
    render::SceneLighting(lighting);
    g_sceneRenderer.device->fog = render::MeshFog::None;

    DrawGround();

    if (figure) {
        // As the world's draw puts its units (world_client_draw.cpp, DrawPass).
        state.stages[0].combiner = kAlphaPassStage;
        state.blend = true;
        state.alphaTest = true;
        state.alphaReference = 0xC8;
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = true;
        // Drawn where it is between its last two ticks.
        const Vector3 at = figure->localPosition;
        const float angle = walkAngle;
        if (walking && !paused) {
            walkAngle = angle - (Stride() / std::max(circleRadius, 0.5f)) * (1.0f - tickFraction);
            PlaceFigure();
        }
        figure->Draw(&g_sceneRenderer, 1.0f);
        walkAngle = angle;
        figure->localPosition = at;
        PlaceFigure();
    }

    if (particles) {
        state.stages[0].combiner = kAlphaPassStage;
        state.blend = true;
        state.alphaTest = false;
        state.sourceBlend = render::Blend::SourceAlpha;
        state.destBlend = render::Blend::InvSourceAlpha;
        state.depthWrite = false;
        particles->DrawAll(&g_sceneRenderer);
        state.depthWrite = true;
    }
    state.stages[0].combiner = kSceneStage;
    pipeline->PopMatrix(kStackWorld);
    pipeline->PopMatrix(kStackScene2);
    pipeline->PopMatrix(kStackScene1);
    render::EndWorld();
}

// ---- The camera's input ----------------------------------------------------------------------------------------------

void ViewerScene::TakeEvent(const platform::Event& event) {
    using platform::EventType;
    using platform::Key;
    switch (event.type) {
    case EventType::MouseDown:
        if (event.button == platform::MouseButton::Right) dragTurning = true;
        if (event.button == platform::MouseButton::Middle) dragMoving = true;
        dragX = event.x;
        dragY = event.y;
        break;
    case EventType::MouseUp:
        if (event.button == platform::MouseButton::Right) dragTurning = false;
        if (event.button == platform::MouseButton::Middle) dragMoving = false;
        break;
    case EventType::MouseMove: {
        const float dx = static_cast<float>(event.x - dragX), dy = static_cast<float>(event.y - dragY);
        dragX = event.x;
        dragY = event.y;
        if (dragTurning) {
            camera.heading -= dx * kTurnPerPixel;
            camera.pitch = std::clamp(camera.pitch + dy * kTurnPerPixel, kLeastPitch, kMostPitch);
        } else if (dragMoving) {
            const Quaternion rotation = camera.Rotation();
            Vector3 right = rotation.AxisX();
            Vector3 forward = rotation.AxisZ();
            right.z = forward.z = 0.0f;
            if (LengthSquared(right) > 0.0f) right = right * (1.0f / Length(right));
            if (LengthSquared(forward) > 0.0f) forward = forward * (1.0f / Length(forward));
            const float scale = camera.distance * 0.0025f;
            camera.focus += (right * -dx + forward * dy) * scale;
        }
        break;
    }
    case EventType::Wheel:
        camera.distance = std::clamp(camera.distance * lib::Pow(0.9f, event.wheel), kNearest, kFarthest);
        break;
    case EventType::KeyDown:
    case EventType::KeyUp: {
        std::uint32_t bit = 0;
        if (event.key == KeyAfter(Key::A, 22) || event.key == Key::Up) bit = 1;
        if (event.key == KeyAfter(Key::A, 18) || event.key == Key::Down) bit = 2;
        if (event.key == Key::A || event.key == Key::Left) bit = 4;
        if (event.key == KeyAfter(Key::A, 3) || event.key == Key::Right) bit = 8;
        if (event.type == EventType::KeyDown) keysHeld |= bit;
        else keysHeld &= ~bit;
        break;
    }
    case EventType::Focus:
        if (!event.active) keysHeld = 0;
        break;
    default: break;
    }
}

} // namespace tools::effect_viewer
