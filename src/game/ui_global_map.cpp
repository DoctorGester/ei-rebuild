// The global map's Create: the island model of the local player's allod, its zones, the quests the player
// holds and the links to the zones the player may enter, the clock text, the camp and turn buttons, the
// weather. See docs/notes/ui.md ("Camp, global map, hero screens").
#include "app.h"
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "global_map_clouds.h"
#include "text_manager.h"
#include "maths.h"
#include "model.h"
#include "object_tables.h"
#include "options.h"
#include "player_client.h"
#include "sound.h"
#include "surface.h"
#include "text_split.h"
#include "texture.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "world.h"
#include "world_glue.h"

#include "render/render_state.h"

#include "lib/containers.h"
#include "platform/cursor.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
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

// _ftol's truncation: the low half of the int64 one (0 for a NaN; the screen places and the script variables
// truncated here are not bounded).
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }

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
    CTexture* Texture(int index) const { return map->objects[index]; }

    UiMesh* Marker(const std::string& figure, const Vector3& local, float scale) const {
        UiMesh* sprite = map->AddSprite(figure);
        sprite->localPosition = ModelPoint(local);
        sprite->texture = Texture(1);
        sprite->SetScale(scale);
        Turn(sprite);
        return sprite;
    }
    UiMesh* Layer(std::uint32_t v0, std::uint32_t u1, std::uint32_t v1, float height) const {
        UiMesh* sprite = map->AddBlankSprite();
        sprite->SetRectangle(25.0f, 100.0f, 700.0f, 0.0f, 100.0f, 600.0f, 0, static_cast<int>(v0),
                             static_cast<int>(u1), static_cast<int>(v1));
        sprite->texture = Texture(4);
        Turn(sprite);
        sprite->localPosition = ModelPoint(Vector3{0.0f, 0.0f, height});
        return sprite;
    }

private:
    GlobalMap* map;
};

void AddTipRegion(UiWindow* window, int left, int top, int right, int bottom, int tip) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    region->SetTip(tip);
}

// A link marker's region: 40 x 35 round the marker's place on the screen.
void SetMarkerRegion(UiRegion* region, const UiMesh* sprite) {
    float x, y, depth;
    sprite->GetPlacement(&x, &y, &depth);
    int bottom = Truncate(y + 10.0f);
    int right = Truncate(x + 20.0f);
    int top = Truncate(y - 25.0f);
    int left = Truncate(x - 20.0f);
    region->SetRect(left, top, right, bottom);
}

// The product (r = b * a in this operand order), each component stored.
Quaternion TurnProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    float z = ((b.x * a.y - a.x * b.y) + b.z * a.w) + a.z * b.w;
    float y = ((a.x * b.z - b.x * a.z) + b.y * a.w) + b.w * a.y;
    float x = ((a.z * b.y - b.z * a.y) + b.x * a.w) + a.x * b.w;
    float w = b.w * a.w - DotUnrounded<Sum3::ZYX, float>(a.x, a.y, a.z, b.x, b.y, b.z);
    r.w = w;
    r.x = x;
    r.y = y;
    r.z = z;
    return r;
}

// A layer's texture frame: the fractional part of time * rate (stored).
void SetFrameFraction(EngineObject* layer, float time, float rate) {
    float v = time * rate;
    float fraction = v - static_cast<float>(Truncate(v));
    AsUiMesh(layer)->textureOffsetU = fraction;
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
        Quaternion q;
        q.SetAxisAngle(axis, 3.1415927f * 0.8f);   // pi * 0.8
        turn = q;
    }
    MapBuilder builder(this);

    // The current zone: its arrow above it, its base on it, the island.
    {
        Vector3 above = {position.x, position.y, position.z + 2.0f};
        builder.Marker(allod->otherNames[kAllodArrow], above, 1.0f);
        builder.Marker(allod->otherNames[kAllodBall], position, 0.8f);
        UiMesh* island = AddSprite(allod->otherNames[kAllodIsland]);
        island->texture = builder.Texture(2);
        island->Place(centreX, centreY, depth);
        builder.Turn(island);
    }

    // Regions 0-2: turn left, turn right, the camp.
    AddTipRegion(this, 0x2E1, 0x219, 0x311, 0x249, 0x15FF6);
    AddTipRegion(this, 0x2B1, 0x219, 0x2E1, 0x249, 0x15FF5);
    AddTipRegion(this, 0xF, 0x20D, 0x6B, 0x249, 0x15FF4);

    std::string variable;
    std::vector<std::string> parts;
    NamedFloatTable vars;   // the local player's variables by name prefix

    // The game zones of the allod that have a marker, with the player's "z.<zone>" state.
    for (int i = 0; i < std::ssize(allod->maps); ++i) {
        MapRecord* zone = &g_mapDatabase.maps[allod->maps[i]];
        if (zone->kind != 0 || zone->figure.empty()) continue;
        variable = std::format("z.{}", zone->name);
        GlobalMapZone& entry = zones.emplace_back();
        entry.link = -1;
        entry.state = Truncate(LocalPlayerVariable(variable));
        entry.name = zone->name;
        entry.title = PlaceTitle("zone ", zone->name);
        UiMesh* sprite = AddSprite(zone->figure);
        sprite->Place(centreX, centreY, depth);
        builder.Turn(sprite);
    }

    // The quests the player holds in each zone ("q.<zone>.<quest>" = 1).
    for (int i = 0; i < std::ssize(zones); ++i) {
        variable = std::format("q.{}", zones[i].name);
        GameLocalPlayerVariables(variable, &vars);
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
            sprite->localPosition = builder.ModelPoint(quest.position);
            sprite->texture = builder.Texture(1);
            sprite->SetScale(0.3f);
            builder.Turn(sprite);
            SetWhite(sprite);
        }
    }

    // The ways out of each zone the player may take ("z.<way>" = 2), with a region each.
    for (int i = 0; i < std::ssize(zones); ++i) {
        MapRecord* zone = g_mapDatabase.FindMap(zones[i].name);
        for (int e = 0; e < std::ssize(zone->exits); ++e) {
            MapRecord* way = g_mapDatabase.FindMap(zone->exits[e].target);
            if (!way) continue;
            variable = std::format("z.{}", way->name);
            if (way->kind != 2) continue;
            float state = LocalPlayerVariable(variable);
            EI_NAN_AUDIT(state);
            if (state != 2.0f) continue;
            if (way->figure.empty()) continue;
            if (zones[i].state == 2) zones[i].link = lib::Count(links);
            GlobalMapPlace& link = links.emplace_back();
            link.zone = i;
            link.name = way->name;
            link.title = PlaceTitle("zone ", way->name);
            link.position = MapPosition("#zone ", way->name, true).value_or(Vector3{});
            UiMesh* sprite = AddSprite(way->figure);
            sprite->localPosition = builder.ModelPoint(link.position);
            sprite->texture = builder.Texture(1);
            sprite->SetScale(0.3f);
            builder.Turn(sprite);
            SetWhite(sprite);
            SetMarkerRegion(AddRegion(), sprite);
        }
    }

    // Three plain layers over the model (shadow, haze, glow), in the weather's colours (the original first gave
    // them 0xA08200C8, 0xF0B400FF and 0x4BFFFFFF; its weather table replaced them before the first frame).
    {
        UiMesh* shadow = builder.Layer(0x100, 0x100, 0, -0.5f);
        shadow->flatColour = 0x649B00E1;
        shadow->textureOffsetV = 0.3f;
        shadow->flags |= FigureMeshFlag::Flat;
        UiMesh* haze = builder.Layer(0, 0x100, 0x100, 0.7f);
        haze->flatColour = 0x6E9B007D;
        haze->textureOffsetV = 0.6f;
        haze->flags |= FigureMeshFlag::Flat;
        UiMesh* glow = builder.Layer(0, 0x100, 0x100, 2.0f);
        glow->flatColour = 0xB9690AC3;
        glow->visible = false;
        glow->flags |= FigureMeshFlag::Flat;
    }

    {
        backdrop = new UiSurface(ui, 0, 0, 400, 300, SurfaceBlend::Keyed, true);
        backdrop->SetRect(0, 0, 800, 600);
    }
    {
        UiMesh* shade = AddBlankSprite();
        shade->texture = nullptr;
        shade->flags |= FigureMeshFlag::Flat;
        shade->flatColour = 0xA0000000;
        shade->visible = false;
        UiMesh* picture = AddBlankSprite();
        picture->visible = false;
        picture->texture = builder.Texture(3);
    }
    {
        UiSurface* info = new UiSurface(ui, 400, 20, 780, 300, SurfaceBlend::Keyed, true);
        AddSurface(info);
        info->Hide(false);
    }
    {
        UiMesh* shade = AddBlankSprite();
        shade->SetRectangle(3.0f, 100.0f, 20.0f, 20.0f, 170.0f, 70.0f, 0, 0, 0x100, 0x100);
        shade->texture = nullptr;
        shade->flatColour = 0xA0000000;
        shade->flags |= FigureMeshFlag::Flat;
        UiMesh* frame = AddBlankSprite();
        frame->SetFrame(3.0f, 100.0f, 15.0f, 15.0f, 175.0f, 75.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
        frame->texture = builder.Texture(3);
    }

    // The clock: "<save_day>  <day>,  <h>:<mm>" under the allod's title.
    int minutes = Truncate(LocalPlayerVariable("gtime") * 60.0f);
    int day = minutes / 1440;
    minutes -= day * 1440;
    int hour = minutes / 60;
    minutes -= hour * 60;
    std::string clock;
    std::string dayText = g_clientWorld.texts->GetString("save_day");
    clock = std::format("{}  {},  {}:{:02}", dayText, day + 1, hour, minutes);
    {
        UiSurface* text = new UiSurface(ui, 20, 20, 170, 70, SurfaceBlend::Keyed, true);
        AddSurface(text);
        RECT line1 = {0, 5, 150, 25};
        text->DrawTextIn(allodTitle, &line1, 2, 0xA7D7E4, 0x25, 1);
        RECT line2 = {0, 25, 150, 45};
        text->DrawTextIn(clock, &line2, 2, 0xA7D7E4, 0x25, 1);
    }

    // The camp button (hidden with "i.nocamp" = 1) and the two turn buttons.
    {
        UiMesh* camp = AddBlankSprite();
        camp->SetRectangle(3.0f, 100.0f, 15.0f, 525.0f, 107.0f, 585.0f, 2, 0x97, 0x5E, 0xD3);
        camp->texture = builder.Texture(3);
        noCamp = Truncate(LocalPlayerVariable("i.nocamp")) == 1;
        if (noCamp) camp->visible = false;
        UiMesh* left = AddBlankSprite();
        left->SetRectangle(3.0f, 100.0f, 689.0f, 537.0f, 737.0f, 585.0f, 0x51, 0x37, 0x81, 0x67);
        left->texture = builder.Texture(3);
        UiMesh* right = AddBlankSprite();
        right->SetRectangle(3.0f, 100.0f, 737.0f, 537.0f, 785.0f, 585.0f, 0x86, 0x37, 0xB6, 0x67);
        right->texture = builder.Texture(3);
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
    GameSetWeather(0, 0.0f);   // clear
    g_clientWorld.music->Stop();
    weather = new GlobalMapClouds();
    weather->Start();
    // The weather: the display's colour (the original first set 0xFF3C005A) and the clouds' ramp.
    g_sceneRenderer.fogColour = 0xFF00000A;
    weather->SetRamp(0x77, 0xF8, 2.0f);
    Update();
    windSound = ui->PlayInterfaceSound("buttons\\globalmap\\wind.wav", true);
    if (autosave && options::g_options.autosave != 0) ::QuickSave("saveb", true);
    ui->OpenHelp(false);
}

// --- Update -----------------------------------------------------------------------------

void GlobalMap::Update() {
    UiContainer::Update();
    if (dragging && turning == 0) {
        platform::Point cursor{};
        platform::CursorPosition(&cursor);
        Input()->MouseMove(0, cursor.x, cursor.y);
    }
    MapBuilder builder(this);
    if (turning != 0 || dragging) {
        float turned = FrameTime() * static_cast<float>(turning) * 3.1415927f * 0.25f + angle;
        angle = std::fmod(turned, kGlobalMapFullTurn);
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Vector3 side = {1.0f, 0.0f, 0.0f};
        Quaternion spin;
        spin.SetAngleAxis(angle, up);
        Quaternion tilt;
        tilt.SetAxisAngle(side, 3.1415927f * 0.8f);
        Quaternion rotation = TurnProduct(spin, tilt);
        turn = rotation;
        EI_NAN_AUDIT(turn.w);
        if (turn.w < 0.0f) {
            turn.w = -turn.w;
            turn.x = -turn.x;
            turn.y = -turn.y;
            turn.z = -turn.z;
        }
        SceneMesh(1)->localPosition = builder.ModelPoint(position);
        builder.Turn(SceneMesh(1));
        builder.Turn(SceneMesh(2));
        for (int k = 3; k < 6; ++k) builder.Turn(SceneMesh(MarkerCount() + k));
        for (int i = 0; i < std::ssize(zones); ++i) builder.Turn(Marker(i));
        for (int i = 0; i < std::ssize(quests); ++i) {
            UiMesh* sprite = Marker(lib::Count(zones) + i);
            sprite->localPosition = builder.ModelPoint(quests[i].position);
            builder.Turn(sprite);
        }
        for (int i = 0; i < std::ssize(links); ++i) {
            UiMesh* sprite = Marker(lib::Count(zones) + lib::Count(quests) + i);
            sprite->localPosition = builder.ModelPoint(links[i].position);
            builder.Turn(sprite);
            SetMarkerRegion(regions[3 + i], sprite);
        }
    }

    float time = FrameTime() + bobTime;
    bobTime = time;
    float bob = 0.5f - pc24::Mul(pc24::Cos(time * 5.0f), 0.2f);   // the cosine's 64 bits times 0.2f: one rounding
    Vector3 above = {position.x, position.y, bob + position.z};
    SceneMesh(0)->localPosition = builder.ModelPoint(above);
    SetFrameFraction(sceneObjects[MarkerCount() + 3], bobTime, 0.01f);
    SetFrameFraction(sceneObjects[MarkerCount() + 4], bobTime, 0.016666668f);
    SetFrameFraction(sceneObjects[MarkerCount() + 5], bobTime, 0.033333335f);

    if (selectPending) RefreshSelection();
    selectPending = false;

    // The weather is drawn into texture 4 on the GPU (the original wrote its pixels under the device lock).
    if (std::ssize(objects) > 4) weather->Draw(objects[4]->surface, FrameTime());
    g_cursor.SetShape(dragging ? 0x16 : 0);
}

// --- Draw -------------------------------------------------------------------------------

void GlobalMap::Draw(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = false;
    state.sourceBlend = render::Blend::SourceAlpha;
    state.destBlend = render::Blend::InvSourceAlpha;
    state.alphaReference = 1;
    UiContainer::Draw(pass);
}
