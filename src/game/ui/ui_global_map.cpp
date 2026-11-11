// The global map's Create: the island model of the local player's allod, its zones, the quests the player
// holds and the links to the zones the player may enter, the clock text, the camp and turn buttons, the
// weather. See docs/notes/ui.md ("Camp, global map, hero screens").
#include "game/app/app.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/ui/global_map_clouds.h"
#include "engine/data/text_manager.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/models/texture.h"
#include "engine/core/named_floats.h"
#include "game/map/map_database.h"
#include "game/app/options.h"
#include "game/players/player_client.h"
#include "game/sound/sound.h"
#include "engine/scene/surface.h"
#include "engine/core/text_split.h"
#include "game/players/player.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_saveload.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_cursor.h"
#include "game/world/world.h"

#include "render/render_state.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <format>
#include "lib/rounding.h"

namespace {

// --- Game data ----------------------------------------------------------------------------------

// An allod of the map database (AllodRecord): its other names are its global map figures, its maps the
// indices of its zones.
constexpr int kAllodArrow = 0;         // the current zone's marker
constexpr int kAllodBall = 1;          // its base
constexpr int kAllodQuestMarker = 2;
constexpr int kAllodIsland = 3;        // the island model

// A zone of the map database (MapRecord): its figure is its marker on the global map (may be empty), its
// kind 0 a game zone and 2 a way between zones here.

// The pipeline's stack a sprite's draw pushes its placement on (UiMesh::Draw) and the mesh draw projects with.
constexpr int kScreenStack = 1;
// What an empty stack's first push amounts to.
constexpr Matrix4 kIdentity = {{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}};

void SetWhite(UiMesh* sprite) {
    for (float& tint : sprite->tint) tint = 1.0f;
}

// The window's helpers bound to its fields.
class MapBuilder {
public:
    explicit MapBuilder(GlobalMap* newMap) : map(newMap) {}

    Vector3 Centre() const {
        float depth = map->depth;
        float x = (map->centreX * 0.0025f - 1.0f) * 0.48157462f * depth;
        float y = (map->centreY * 0.0025f - 0.75f) * 0.48157462f * depth;
        return Vector3{x, y, depth};
    }
    Vector3 ModelPoint(const Vector3& local) const {
        Vector3 centre = Centre();
        return centre + map->turn.Rotate(local);
    }
    void Turn(UiMesh* sprite) const { sprite->localRotation = map->turn; }
    Texture* MapTexture(int index) const { return map->objects[index]; }

    UiMesh* Marker(const std::string& figure, const Vector3& local, float scale) const {
        UiMesh* sprite = map->AddSprite(figure);
        sprite->localPosition = ModelPoint(local);
        sprite->texture = MapTexture(1);
        sprite->SetScale(scale);
        Turn(sprite);
        return sprite;
    }
    UiMesh* Layer(std::uint32_t v0, std::uint32_t u1, std::uint32_t v1, float height) const {
        UiMesh* sprite = map->AddBlankSprite();
        sprite->SetRectangle(25.0f, kSpriteScale, 700.0f, 0.0f, 100.0f, 600.0f, 0, static_cast<int>(v0),
                             static_cast<int>(u1), static_cast<int>(v1));
        sprite->texture = MapTexture(4);
        Turn(sprite);
        sprite->localPosition = ModelPoint(Vector3{0.0f, 0.0f, height});
        return sprite;
    }

private:
    GlobalMap* map = nullptr;
};

UiRegion* AddTipRegion(UiWindow* window, int left, int top, int right, int bottom, int tip) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    region->SetTip(tip);
    return region;
}

// A link marker's region: 40 x 35 round the marker's place on the screen.
void SetMarkerRegion(UiRegion* region, const UiMesh* sprite) {
    const UiMesh::Placement placed = sprite->GetPlacement();
    const float x = placed.x, y = placed.y;
    int bottom = lib::Truncate(y + 10.0f);
    int right = lib::Truncate(x + 20.0f);
    int top = lib::Truncate(y - 25.0f);
    int left = lib::Truncate(x - 20.0f);
    region->SetRect(left, top, right, bottom);
}

// A layer's texture frame: the fractional part of time * rate (stored).
void SetFrameFraction(UiMesh* layer, float time, float rate) {
    float v = time * rate;
    float fraction = v - static_cast<float>(lib::Truncate(v));
    layer->textureOffsetU = fraction;
}

} // namespace

// --- Create ---------------------------------------------------------------------------

void GlobalMap::CreateScreen() {
    // Arrived from a zone (not loaded, not back from the camp): saved to "saveb" once built.
    bool autosave = false;
    if (g_saveStream.size != 0)
        g_saveStream.Resize(0);
    else if (g_clientWorld.LocalView()->player->lastScreen != 5)
        autosave = true;

    MapRecord* current = g_mapDatabase.FindMap(LocalPlayerZone());
    AllodRecord* allod = g_mapDatabase.FindAllod(current->allod);
    allodTitle = PlaceTitle("allod ", allod->name);
    position = MapPosition("#zone ", current->name, true).value_or(Vector3{});
    const std::string& allodName = allod->name;
    LoadTexture((allodName + "Map"), TextureKind::Interface);
    LoadTexture((allodName + "Sys"), TextureKind::Interface);
    LoadTexture((allodName + "MapG"), TextureKind::Interface);
    LoadTexture("saveload", TextureKind::Interface);
    CreateTexture(0x100, 0x100, TextureKind::Blank4444);
    {
        Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion q = Quaternion::AxisAngle(axis, 3.1415927f * 0.8f);   // pi * 0.8
        turn = q;
    }
    MapBuilder builder(this);

    // The current zone: its arrow above it, its base on it, the island.
    {
        Vector3 above = {position.x, position.y, position.z + 2.0f};
        arrow = builder.Marker(allod->otherNames[kAllodArrow], above, 1.0f);
        ball = builder.Marker(allod->otherNames[kAllodBall], position, 0.8f);
        island = AddSprite(allod->otherNames[kAllodIsland]);
        island->texture = builder.MapTexture(2);
        island->Place(centreX, centreY, depth);
        builder.Turn(island);
    }

    turnLeftButton = AddTipRegion(this, 0x2E1, 0x219, 0x311, 0x249, 0x15FF6);
    turnRightButton = AddTipRegion(this, 0x2B1, 0x219, 0x2E1, 0x249, 0x15FF5);
    campButton = AddTipRegion(this, 0xF, 0x20D, 0x6B, 0x249, 0x15FF4);

    std::string variable;
    std::vector<std::string> parts;
    NamedFloatTable vars;   // the local player's variables by name prefix

    // The game zones of the allod that have a marker, with the player's "z.<zone>" state.
    for (int mapIndex : allod->maps) {
        MapRecord* zone = &g_mapDatabase.maps[mapIndex];
        if (zone->kind != 0 || zone->figure.empty()) continue;
        variable = std::format("z.{}", zone->name);
        GlobalMapZone& entry = zones.emplace_back();
        entry.link = -1;
        entry.state = lib::Truncate(LocalPlayerVariable(variable));
        entry.name = zone->name;
        entry.title = PlaceTitle("zone ", zone->name);
        UiMesh* sprite = AddSprite(zone->figure);
        zoneMarkers.push_back(sprite);
        sprite->Place(centreX, centreY, depth);
        builder.Turn(sprite);
    }

    // The quests the player holds in each zone ("q.<zone>.<quest>" = 1).
    for (int i = 0; i < std::ssize(zones); ++i) {
        variable = std::format("q.{}", zones[i].name);
        g_clientWorld.LocalView()->player->GetParty()->variables.CopyPrefixed(variable, &vars);
        for (const auto& node : vars) {
            if (std::bit_cast<std::uint32_t>(node.second) != std::bit_cast<std::uint32_t>(1.0f)) continue;
            parts = text::Split(node.first, '.');
            if (parts.size() != 3) continue;
            GlobalMapPlace& quest = quests.emplace_back();
            quest.name = parts[2];
            quest.zone = i;
            quest.title = PlaceTitle("quest ", parts[2]);
            quest.position = MapPosition("#quest ", parts[2], false).value_or(Vector3{});
            UiMesh* sprite = AddSprite(allod->otherNames[kAllodQuestMarker]);
            questMarkers.push_back(sprite);
            sprite->localPosition = builder.ModelPoint(quest.position);
            sprite->texture = builder.MapTexture(1);
            sprite->SetScale(0.3f);
            builder.Turn(sprite);
            SetWhite(sprite);
        }
    }

    // The ways out of each zone the player may take ("z.<way>" = 2), with a region each.
    for (int i = 0; i < std::ssize(zones); ++i) {
        MapRecord* zone = g_mapDatabase.FindMap(zones[i].name);
        for (const ZoneExit& zoneExit : zone->exits) {
            MapRecord* way = g_mapDatabase.FindMap(zoneExit.target);
            if (!way) continue;
            variable = std::format("z.{}", way->name);
            if (way->kind != 2) continue;
            float state = LocalPlayerVariable(variable);
            if (state != 2.0f) continue;
            if (way->figure.empty()) continue;
            if (zones[i].state == 2) zones[i].link = lib::Count(links);
            GlobalMapPlace& link = links.emplace_back();
            link.zone = i;
            link.name = way->name;
            link.title = PlaceTitle("zone ", way->name);
            link.position = MapPosition("#zone ", way->name, true).value_or(Vector3{});
            UiMesh* sprite = AddSprite(way->figure);
            linkMarkers.push_back(sprite);
            sprite->localPosition = builder.ModelPoint(link.position);
            sprite->texture = builder.MapTexture(1);
            sprite->SetScale(0.3f);
            builder.Turn(sprite);
            SetWhite(sprite);
            linkAreas.push_back(AddRegion());
            SetMarkerRegion(linkAreas.back(), sprite);
        }
    }

    // Three plain layers over the model (shadow, haze, glow), in the weather's colours (the original first gave
    // them 0xA08200C8, 0xF0B400FF and 0x4BFFFFFF; its weather table replaced them before the first frame).
    {
        UiMesh* shadow = layers[0] = builder.Layer(0x100, 0x100, 0, -0.5f);
        shadow->flatColour = 0x649B00E1;
        shadow->textureOffsetV = 0.3f;
        shadow->flags |= FigureMeshFlag::Flat;
        UiMesh* haze = layers[1] = builder.Layer(0, 0x100, 0x100, 0.7f);
        haze->flatColour = 0x6E9B007D;
        haze->textureOffsetV = 0.6f;
        haze->flags |= FigureMeshFlag::Flat;
        UiMesh* glow = layers[2] = builder.Layer(0, 0x100, 0x100, 2.0f);
        glow->flatColour = 0xB9690AC3;
        glow->visible = false;
        glow->flags |= FigureMeshFlag::Flat;
    }

    {
        tipBack = AddBlankSprite();
        tipBack->texture = nullptr;
        tipBack->flags |= FigureMeshFlag::Flat;
        tipBack->flatColour = 0xA0000000;
        tipBack->visible = false;
        tipFrame = AddBlankSprite();
        tipFrame->visible = false;
        tipFrame->texture = builder.MapTexture(3);
    }
    {
        UiSurface* info = AddSurface(std::make_unique<UiSurface>(400, 20, 780, 300, SurfaceBlend::Keyed, true));
        info->PaintWith([this](UiSurface& out) { PaintTip(out); });
        info->Hide();
    }
    {
        UiMesh* shade = AddBlankSprite();
        shade->SetRectangle(kDialogDepth, kSpriteScale, 20.0f, 20.0f, 170.0f, 70.0f, 0, 0, kUiTextureSize, kUiTextureSize);
        shade->texture = nullptr;
        shade->flatColour = 0xA0000000;
        shade->flags |= FigureMeshFlag::Flat;
        UiMesh* frame = AddBlankSprite();
        frame->SetFrame(3.0f, 100.0f, 15.0f, 15.0f, 175.0f, 75.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
        frame->texture = builder.MapTexture(3);
    }

    // The clock: "<save_day>  <day>,  <h>:<mm>" under the allod's title.
    int minutes = lib::Truncate(LocalPlayerVariable("gtime") * 60.0f);
    int day = minutes / 1440;
    minutes -= day * 1440;
    int hour = minutes / 60;
    minutes -= hour * 60;
    std::string dayText = g_clientWorld.texts->GetString("save_day");
    clock = std::format("{}  {},  {}:{:02}", dayText, day + 1, hour, minutes);
    AddSurface(std::make_unique<UiSurface>(20, 20, 170, 70, SurfaceBlend::Keyed, true))->PaintWith([this](UiSurface& out) { PaintTitle(out); });

    // The camp button (hidden with "i.nocamp" = 1) and the two turn buttons.
    {
        UiMesh* camp = AddBlankSprite();
        camp->SetRectangle(kDialogDepth, kSpriteScale, 15.0f, 525.0f, 107.0f, 585.0f, 2, 0x97, 0x5E, 0xD3);
        camp->texture = builder.MapTexture(3);
        noCamp = lib::Truncate(LocalPlayerVariable("i.nocamp")) == 1;
        if (noCamp) camp->visible = false;
        UiMesh* left = AddBlankSprite();
        left->SetRectangle(kDialogDepth, kSpriteScale, 689.0f, 537.0f, 737.0f, 585.0f, 0x51, 0x37, 0x81, 0x67);
        left->texture = builder.MapTexture(3);
        UiMesh* right = AddBlankSprite();
        right->SetRectangle(kDialogDepth, kSpriteScale, 737.0f, 537.0f, 785.0f, 585.0f, 0x86, 0x37, 0xB6, 0x67);
        right->texture = builder.MapTexture(3);
    }

    if (str::Compare(allod->name, "ingos") == 0) {
        fog[0] = 0.37109375f;
        fog[1] = 0.5078125f;
        fog[2] = 0.625f;
    } else if (str::Compare(allod->name, "suslanger") == 0) {
        fog[0] = 0.390625f;
        fog[1] = 0.1953125f;
        fog[2] = 0.0859375f;
    }
    if (str::Compare(allod->name, "gipat") == 0) gipat = true;

    selectPending = true;
    SelectZone(-1, false);
    g_serverWorld.SetWeather(0, 0.0f);   // clear
    g_clientWorld.music->Stop();
    weather = new GlobalMapClouds();
    weather->Start();
    // The weather: the display's colour (the original first set 0xFF3C005A) and the clouds' ramp.
    g_sceneRenderer.fogColour = 0xFF00000A;
    weather->SetRamp(0x77, 0xF8, 2.0f);
    Update();
    windSound = g_interface.PlayInterfaceSound("buttons\\globalmap\\wind.wav", true);
    if (autosave && options::g_options.autosave != 0) ::QuickSave("saveb", true);
    g_interface.OpenHelp(false);
}

// --- Update -----------------------------------------------------------------------------

void GlobalMap::Update() {
    UiContainer::Update();
    if (dragging && turning == 0) {
        UiEvent move{.kind = UiEventKind::MouseMove, .at = platform::CursorPosition().value_or(Point{})};
        OnEvent(move);
    }
    MapBuilder builder(this);
    if (turning != 0 || dragging) {
        float turned = FrameTime() * static_cast<float>(turning) * 3.1415927f * 0.25f + angle;
        angle = std::fmod(turned, kGlobalMapFullTurn);
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Vector3 side = {1.0f, 0.0f, 0.0f};
        Quaternion spin = Quaternion::AxisAngle(up, angle);
        Quaternion tilt = Quaternion::AxisAngle(side, 3.1415927f * 0.8f);
        Quaternion rotation = tilt * spin;
        turn = rotation;
        if (turn.w < 0.0f) {
            turn.w = -turn.w;
            turn.x = -turn.x;
            turn.y = -turn.y;
            turn.z = -turn.z;
        }
        ball->localPosition = builder.ModelPoint(position);
        builder.Turn(ball);
        builder.Turn(island);
        for (UiMesh* layer : layers) builder.Turn(layer);
        for (UiMesh* marker : zoneMarkers) builder.Turn(marker);
        for (int i = 0; i < std::ssize(quests); ++i) {
            UiMesh* sprite = questMarkers[i];
            sprite->localPosition = builder.ModelPoint(quests[i].position);
            builder.Turn(sprite);
        }
        for (int i = 0; i < std::ssize(links); ++i) {
            UiMesh* sprite = linkMarkers[i];
            sprite->localPosition = builder.ModelPoint(links[i].position);
            builder.Turn(sprite);
            SetMarkerRegion(linkAreas[i], sprite);
        }
    }

    float time = FrameTime() + bobTime;
    bobTime = time;
    float bob = 0.5f - (lib::Cos(time * 5.0f) * 0.2f);
    Vector3 above = {position.x, position.y, bob + position.z};
    arrow->localPosition = builder.ModelPoint(above);
    SetFrameFraction(layers[0], bobTime, 0.01f);
    SetFrameFraction(layers[1], bobTime, 0.016666668f);
    SetFrameFraction(layers[2], bobTime, 0.033333335f);

    if (selectPending) RefreshSelection();
    selectPending = false;

    // The weather is drawn into texture 4 on the GPU (the original wrote its pixels under the device lock).
    if (std::ssize(objects) > 4) weather->Draw(objects[4]->surface, FrameTime());
    g_cursor.SetShape(dragging ? CursorShape::Camera : CursorShape::Default);
}

// --- Draw -------------------------------------------------------------------------------

void GlobalMap::DrawSprites(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = false;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.alphaReference = 1;
    const MatrixStack& screen = pass->pipeline->stacks[kScreenStack];
    markerCamera = screen.count != 0 ? screen.matrices[screen.count - 1] : kIdentity;
    UiContainer::DrawSprites(pass);
}

int GlobalMap::ZoneAt(Point point) const {
    if (!markerCamera) return -1;
    int zone = -1;
    float nearest = 0.0f;
    for (int i = 0; i < std::ssize(zones); ++i) {
        const std::optional<float> faceDepth = zoneMarkers[i]->DepthAt(*markerCamera, point);
        if (faceDepth && (zone < 0 || *faceDepth <= nearest)) {
            zone = i;
            nearest = *faceDepth;
        }
    }
    return zone;
}
