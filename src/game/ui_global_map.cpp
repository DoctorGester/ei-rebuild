// The global map's Create (slot 25 of 00740B3C, 0062D350): the island model of the local player's
// allod, its zones, the quests the player holds and the links to the zones the player may enter,
// the clock text, the camp and turn buttons, the weather. See docs/notes/ui.md ("Camp, global map, hero
// screens").
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float. Constants are read from the exe's memory,
// as the original does.
#include "app.h"
#include "global_map_clouds.h"
#include "world_glue.h"
#include "sound.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "core_maths.h"
#include "dx_surface.h"
#include "item_db_text.h"
#include "maths.h"
#include "model.h"
#include "object_tables.h"
#include "texture.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "game_singletons.h"
#include "ei/vcall.h"
#include "lib/nan_audit.h"
#include "lib/mfc_string.h"

#include <concepts>

#include <cstdint>
#include <cstring>
#include <new>
#include <bit>

using console::CStringArray;

namespace mapspec {
Vector3* __fastcall QuaternionRotate(const Quaternion* /*quaternion*/, void*, Vector3* /*out*/, const Vector3* /*v*/);
}
namespace {

constexpr std::uintptr_t kWeatherVtable = std::uintptr_t(0x007420D4);   // GlobalMapWeather (CCloudNoise)

inline double D(float f) { return f; }

float Bits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Bits(T) = delete;


// --- Game data ----------------------------------------------------------------------------------

// An allod of the map database (AllodRecord, object_tables.h): its other names are its global map
// figures, its maps the indices of its zones.
constexpr int kAllodArrow = 0;         // the current zone's marker
constexpr int kAllodBall = 1;          // its base
constexpr int kAllodQuestMarker = 2;
constexpr int kAllodIsland = 3;        // the island model

// A zone of the map database: its figure is its marker on the global map (may be empty), its
// kind 0 a game zone and 2 a way between zones here.
MapRecord* FindZone(const char* name) { return TheMapDatabase()->FindMap(name); }
MapRecord* ZoneAt(int index) { return TheMapDatabase()->maps.data + index; }

bool NonEmpty(const mfc::CString& text) { return text.GetLength() != 0; }

// --- Not rewritten ------------------------------------------------------------------------------

// The links' and the quests' SetSize (00631E70 / 00631FF0, mfc::impl::GlobalMapPointRecords') and
// their elements' assignment (GlobalMapPlace::Assign / AssignCopy, 00632490 / 006324E0, RET 4).
void SetPlaceCount(mfc::CArray<GlobalMapPlace>& places, int size) {
    mfc::impl::ArrayAs<mfc::impl::GlobalMapPointRecords>(places).SetSize(size, -1);
}

// Drawing text on a surface (005FDE20 is Rebirth-patched).
int SurfaceText(CUiSurface* surface, const char* text, RECT* rect, int font, std::uint32_t colour, int format, int flag) {
    return SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, flag);
}

// The local player's variables, copied by name prefix into a local hash_map<CString, float> (100
// buckets, 00581790), freed the way the original's inlined destructor does.
class VariableTable {
public:
    VariableTable() { table_.ConstructDefault(); }
    ~VariableTable() { table_.Destroy(); }
    NamedFloatTable* Table() { return &table_; }

private:
    NamedFloatTable table_;
};

// The rest (not rewritten).
struct SoundMixer {
    void ResetMusic() { reinterpret_cast<CMusicSystem*>(this)->Stop(); }   // 006B4F70 (the object at *007AF760)
};
// The scene renderer (007AF060, what 00500960 returns).
// 006570E0: saves the game into saves\<name> (ECX name, DL flag).

// --- Sprite fields set directly -------------------------------------------------------------------

// Float fields are written with their bits, as the original's immediate stores do.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, 4); }
void SetTexture(CUiMesh* sprite, CEngineObject* texture) { sprite->texture_ = texture; }
void SetPlain(CUiMesh* sprite) { sprite->flags_ |= 0x100; }
void SetPosition(CUiMesh* sprite, const Vector3& p) { std::memcpy(&sprite->localPosition_, &p, 12); }
void SetWhite(CUiMesh* sprite) {
    for (float& tint : sprite->tint_) SetBits(tint, 0x3F800000);
}
CUiMesh* AddFigure(CUiWindow* window, const char* name) { return window->AddSprite(name); }

} // namespace

// --- Create (0062D350) ---------------------------------------------------------------------------

namespace {

// The window's helpers bound to its fields.
class MapBuilder {
public:
    explicit MapBuilder(CGlobalMap* map) : map_(map) {}

    // The layout point (centreX, centreY) at `depth` in camera space, stored as floats.
    Vector3 Centre() const {
        float depth = map_->depth_;
        float x = static_cast<float>((D(map_->centreX_) * D(0.0025f /*0074015C*/) - D(1.0f /*0073B84C*/)) *
                        D(0.48157462f /*0074209C*/) * D(depth));
        float y = static_cast<float>((D(map_->centreY_) * D(0.0025f /*0074015C*/) - D(0.75f /*0073C524*/)) *
                        D(0.48157462f /*0074209C*/) * D(depth));
        return Vector3{x, y, depth};
    }
    // A point of the map model (`local`) in camera space: the centre plus the turned point.
    Vector3 ModelPoint(const Vector3& local) const {
        Vector3 centre = Centre();
        Vector3 turned;
        mapspec::QuaternionRotate(&map_->turn_, nullptr, &turned, &local);
        Vector3 out;
        AddVectors(&out, centre, turned);
        return out;
    }
    void Turn(CUiMesh* sprite) const { std::memcpy(&sprite->localRotation_, &map_->turn_, 16); }
    CEngineObject* Texture(int index) const { return map_->objects_.data[index]; }

    // A marker of the map model: the figure at `local`, scaled, turned, white; returns the sprite.
    CUiMesh* Marker(const char* figure, const Vector3& local, float scale) const {
        CUiMesh* sprite = AddFigure(map_, figure);
        SetPosition(sprite, ModelPoint(local));
        SetTexture(sprite, Texture(1));
        sprite->SetScale(scale);
        Turn(sprite);
        return sprite;
    }
    // A full-screen plain layer of the model at `height` over it.
    CUiMesh* Layer(std::uint32_t v0, std::uint32_t u1, std::uint32_t v1, float height) const {
        CUiMesh* sprite = map_->AddSprite(0);
        sprite->SetRectangle(25.0f, 100.0f, 700.0f, 0.0f, 100.0f, 600.0f, 0, static_cast<int>(v0),
                             static_cast<int>(u1), static_cast<int>(v1));
        SetTexture(sprite, Texture(4));
        Turn(sprite);
        Vector3 centre = Centre();
        Vector3 local = {0.0f, 0.0f, height};
        Vector3 turned;
        mapspec::QuaternionRotate(&map_->turn_, nullptr, &turned, &local);
        Vector3 p;
        p.Construct(static_cast<float>(D(centre.x) + D(turned.x)), static_cast<float>(D(centre.y) + D(turned.y)),
                    static_cast<float>(D(centre.z) + D(turned.z)));
        SetPosition(sprite, p);
        return sprite;
    }

private:
    CGlobalMap* map_;
};


void AddTipRegion(CUiWindow* window, int left, int top, int right, int bottom, int tip) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    region->SetTip(tip, 1, 0);
}

// A region over a marker sprite (its layout position): 20 to each side, 25 above, 10 below.
void AddMarkerRegion(CGlobalMap* map, CUiMesh* sprite) {
    float x, y, depth;
    sprite->GetPlacement(&x, &y, &depth);
    int bottom = static_cast<int>(gamecrt::Ftol(D(y) + D(10.0f /*0073BAAC*/)));
    int right = static_cast<int>(gamecrt::Ftol(D(x) + D(20.0f /*0073CCF8*/)));
    int top = static_cast<int>(gamecrt::Ftol(D(y) - D(25.0f /*0073E124*/)));
    int left = static_cast<int>(gamecrt::Ftol(D(x) - D(20.0f /*0073CCF8*/)));
    map->AddRegion()->CallSetRect(left, top, right, bottom);
}

} // namespace

void CGlobalMap::Create() {
    autosave_ = false;
    if (ei::GameData<const int>(0x007AFE1C) != 0)
        GameSaveStream()->Resize(0);
    else if (reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player->lastScreen_ != 5)
        autosave_ = true;   // not back from the camp
    depth_ = Bits(0x41200000);     // 10
    centreX_ = Bits(0x43C80000);   // 400
    centreY_ = Bits(0x438C0000);   // 280
    turning_ = 0;
    bobTime_ = 0.0f;

    MapRecord* current;
    {
        mfc::CString name(mfc::CString::kUnset);
        current = FindZone(*LocalPlayerName(&name));
    }
    AllodRecord* allod = TheMapDatabase()->FindAllod(current->allod);
    {
        mfc::CString title(mfc::CString::kUnset);
        allodTitle_ = *AllodTitle(&title, allod->name);
    }
    {
        Vector3 position;
        ZonePosition(&position, current->name);
        std::memcpy(&position_, &position, 12);
    }
    const mfc::CString& allodName = allod->name;
    LoadTexture(mfc::operator+(allodName, "Map"), 3, 0);   // <allod>Map
    LoadTexture(mfc::operator+(allodName, "Sys"), 3, 0);   // <allod>Sys
    LoadTexture(mfc::operator+(allodName, "MapG"), 3, 0);   // <allod>MapG
    LoadTexture("saveload", 3, 0);
    CreateTexture(0x100, 0x100, 0x21);
    angle_ = 0.0f;
    {
        Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion q;
        q.SetAxisAngle(axis, static_cast<float>(D(3.1415927f /*00742098*/) * D(0.8f /*0073CCA8*/)));   // pi * 0.8
        std::memcpy(&turn_, &q, 16);
    }
    MapBuilder builder(this);

    // The current zone: its arrow above it, its base on it, the island.
    {
        Vector3 above = {position_.x, position_.y, static_cast<float>(D(position_.z) + D(2.0f /*0073CDB8*/))};
        builder.Marker(allod->otherNames[kAllodArrow], above, 1.0f);
        Vector3 at = {position_.x, position_.y, position_.z};
        builder.Marker(allod->otherNames[kAllodBall], at, Bits(0x3F4CCCCD));   // 0.8
        CUiMesh* island = AddFigure(this, allod->otherNames[kAllodIsland]);
        SetTexture(island, builder.Texture(2));
        island->Place(centreX_, centreY_, depth_);
        builder.Turn(island);
    }

    // Regions 0-2: turn left, turn right, the camp.
    AddTipRegion(this, 0x2E1, 0x219, 0x311, 0x249, 0x15FF6);
    AddTipRegion(this, 0x2B1, 0x219, 0x2E1, 0x249, 0x15FF5);
    AddTipRegion(this, 0xF, 0x20D, 0x6B, 0x249, 0x15FF4);

    mfc::CString variable;
    CStringArray parts;
    VariableTable vars;
    // New elements are copied from these (the original's have uninitialised numbers; every
    // number is overwritten right after).
    GlobalMapZone zoneTemplate{};
    GlobalMapPlace linkTemplate{};
    GlobalMapPlace questTemplate{};
    linkCount_ = 0;
    questCount_ = 0;
    zoneCount_ = 0;

    // The game zones of the allod that have a marker, with the player's "z.<zone>" state.
    for (int i = 0; i < allod->maps.size; ++i) {
        MapRecord* zone = ZoneAt(allod->maps.data[i]);
        if (zone->kind != 0 || !NonEmpty(zone->figure)) continue;
        GlobalMapZonesSetAtGrow(&zones_, nullptr, zones_.size, &zoneTemplate);
        variable.Format("z.%s", static_cast<const char*>(zone->name));
        zones_.data[zoneCount_].link = -1;
        zones_.data[zoneCount_].state = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable(variable))));
        zones_.data[zoneCount_].name = zone->name;
        {
            mfc::CString title(mfc::CString::kUnset);
            zones_.data[zoneCount_].title = *ZoneTitle(&title, zone->name);
        }
        CUiMesh* sprite = AddFigure(this, zone->figure);
        sprite->Place(centreX_, centreY_, depth_);
        builder.Turn(sprite);
        ++zoneCount_;
    }

    // The quests the player holds in each zone ("q.<zone>.<quest>" = 1).
    for (int i = 0; i < zoneCount_; ++i) {
        variable.Format("q.%s", static_cast<const char*>(zones_.data[i].name));
        GameLocalPlayerVariables(variable, vars.Table());
        const NamedFloatTable& table = *vars.Table();
        for (const NamedFloatTable::Node* node = table.First(); node; node = table.Next(node)) {
            std::uint32_t value;
            std::memcpy(&value, &node->value.mapped, 4);
            if (value == 0x3F800000) {
                text::Split(node->value.key, &parts, '.');
                if (parts.GetSize() == 3) {
                    int n = questCount_;
                    SetPlaceCount(quests_, quests_.size + 1);
                    quests_.data[quests_.size - 1].AssignCopy(questTemplate);
                    GlobalMapPlace& quest = quests_.data[n];
                    quest.name = parts[2];
                    quests_.data[n].zone = i;
                    {
                        mfc::CString title(mfc::CString::kUnset);
                        quests_.data[n].title = *QuestTitle(&title, parts[2]);
                    }
                    {
                        Vector3 position;
                        QuestPosition(&position, parts[2]);
                        std::memcpy(&quests_.data[n].position, &position, 12);
                    }
                    CUiMesh* sprite = AddFigure(this, allod->otherNames[kAllodQuestMarker]);
                    Vector3 local = quests_.data[n].position;
                    SetPosition(sprite, builder.ModelPoint(local));
                    SetTexture(sprite, builder.Texture(1));
                    sprite->SetScale(0.3f /*007420D0*/);
                    builder.Turn(sprite);
                    SetWhite(sprite);
                    float x, y, depth;
                    sprite->GetPlacement(&x, &y, &depth);
                    ++questCount_;
                }
            }
        }
    }

    // The ways out of each zone the player may take ("z.<way>" = 2), with a region each.
    for (int i = 0; i < zoneCount_; ++i) {
        MapRecord* zone = FindZone(zones_.data[i].name);
        for (int e = 0; e < zone->exits.size; ++e) {
            MapRecord* way = FindZone(zone->exits.data[e].target);
            if (!way) continue;
            variable.Format("z.%s", static_cast<const char*>(way->name));
            if (way->kind != 2) continue;
            // Only at state 2.
            double state = D(LocalPlayerVariable(variable));
            double two = D(2.0f /*0073CDB8*/);
            EI_NAN_AUDIT(state);
            if (state != two) continue;
            if (!NonEmpty(way->figure)) continue;
            if (zones_.data[i].state == 2) zones_.data[i].link = linkCount_;
            int n = linkCount_;
            SetPlaceCount(links_, links_.size + 1);
            links_.data[links_.size - 1].Assign(linkTemplate);
            links_.data[n].zone = i;
            links_.data[n].name = way->name;
            {
                mfc::CString title(mfc::CString::kUnset);
                links_.data[n].title = *ZoneTitle(&title, way->name);
            }
            {
                Vector3 position;
                ZonePosition(&position, way->name);
                std::memcpy(&links_.data[n].position, &position, 12);
            }
            CUiMesh* sprite = AddFigure(this, way->figure);
            Vector3 local = links_.data[n].position;
            SetPosition(sprite, builder.ModelPoint(local));
            SetTexture(sprite, builder.Texture(1));
            sprite->SetScale(0.3f /*007420D0*/);
            builder.Turn(sprite);
            SetWhite(sprite);
            AddMarkerRegion(this, sprite);
            ++linkCount_;
        }
    }
    markerCount_ = questCount_ + linkCount_ + zoneCount_;

    // Three plain layers over the model (shadow, haze, glow).
    {
        CUiMesh* shadow = builder.Layer(0x100, 0x100, 0, Bits(0xBF000000));   // -0.5
        shadow->flatColour_ = 0xA08200C8;
        SetBits(shadow->textureOffsetV_, 0x3E99999A);   // 0.3
        SetPlain(shadow);
        CUiMesh* haze = builder.Layer(0, 0x100, 0x100, Bits(0x3F333333));    // 0.7
        haze->flatColour_ = 0xF0B400FF;
        SetBits(haze->textureOffsetV_, 0x3F19999A);     // 0.6
        SetPlain(haze);
        CUiMesh* glow = builder.Layer(0, 0x100, 0x100, Bits(0x40000000));    // 2.0
        glow->flatColour_ = 0x4BFFFFFF;
        glow->visible_ = 0;
        SetPlain(glow);
    }
    TheSceneRenderer()->fogColour_ = 0xFF3C005A;

    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        backdrop_ = memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, 400, 300, 5, 1) : nullptr;
        backdrop_->SetRect(0, 0, 800, 600);
    }
    {
        CUiMesh* shade = AddSprite(0);
        SetTexture(shade, nullptr);
        SetPlain(shade);
        shade->flatColour_ = 0xA0000000;
        shade->visible_ = 0;
        CUiMesh* picture = AddSprite(0);
        picture->visible_ = 0;
        SetTexture(picture, builder.Texture(3));
    }
    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        CUiSurface* info = memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 400, 20, 780, 300, 5, 1)
                                  : nullptr;
        AddSurface(info);
        info->Freeze(0, 0);
    }
    {
        CUiMesh* shade = AddSprite(0);
        shade->SetRectangle(3.0f, 100.0f, 20.0f, 20.0f, 170.0f, 70.0f, 0, 0, 0x100, 0x100);
        SetTexture(shade, nullptr);
        shade->flatColour_ = 0xA0000000;
        SetPlain(shade);
        CUiMesh* frame = AddSprite(0);
        frame->SetFrame(3.0f, 100.0f, 15.0f, 15.0f, 175.0f, 75.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
        SetTexture(frame, builder.Texture(3));
    }

    // The clock: "<save_day>  <day>,  <h>:<mm>" under the allod's title.
    int minutes = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable("gtime")) * D(60.0f /*0073F090*/)));   // gtime * 60
    int day = minutes / 1440;
    minutes -= day * 1440;
    int hour = minutes / 60;
    minutes -= hour * 60;
    mfc::CString clock;
    mfc::CString dayText;
    TheTextManager()->GetString("save_day", &dayText);
    clock.Format("%s  %d,  %d:%02d", static_cast<const char*>(dayText), day + 1, hour, minutes);
    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        CUiSurface* text = memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 20, 20, 170, 70, 5, 1)
                                  : nullptr;
        AddSurface(text);
        UiSurfaceGetDC(text);
        RECT line1 = {0, 5, 150, 25};
        SurfaceText(text, allodTitle_, &line1, 2, 0xA7D7E4, 0x25, 1);
        RECT line2 = {0, 25, 150, 45};
        SurfaceText(text, clock, &line2, 2, 0xA7D7E4, 0x25, 1);
        UiSurfaceReleaseDC(text);
    }

    // The camp button (hidden with "i.nocamp" = 1) and the two turn buttons.
    {
        CUiMesh* camp = AddSprite(0);
        camp->SetRectangle(3.0f, 100.0f, 15.0f, 525.0f, 107.0f, 585.0f, 2, 0x97, 0x5E, 0xD3);
        SetTexture(camp, builder.Texture(3));
        noCamp_ = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable("i.nocamp"))));
        if (noCamp_ == 1) camp->visible_ = 0;
        CUiMesh* left = AddSprite(0);
        left->SetRectangle(3.0f, 100.0f, 689.0f, 537.0f, 737.0f, 585.0f, 0x51, 0x37, 0x81, 0x67);
        SetTexture(left, builder.Texture(3));
        CUiMesh* right = AddSprite(0);
        right->SetRectangle(3.0f, 100.0f, 737.0f, 537.0f, 785.0f, 585.0f, 0x86, 0x37, 0xB6, 0x67);
        SetTexture(right, builder.Texture(3));
    }

    // The fog colour of the allod.
    gipat_ = false;
    fog_[0] = 0.0f;
    fog_[1] = 1.0f;
    fog_[2] = 0.0f;
    if (gamecrt::mbscmp(allod->name,
                        "ingos") == 0) {
        fog_[0] = Bits(0x3EBE0000);
        fog_[1] = Bits(0x3F020000);
        fog_[2] = Bits(0x3F200000);
    } else if (gamecrt::mbscmp(allod->name,
                               "suslanger") == 0) {
        fog_[0] = Bits(0x3EC80000);
        fog_[1] = Bits(0x3E480000);
        fog_[2] = Bits(0x3DB00000);
    }
    if (gamecrt::mbscmp(allod->name,
                        "gipat") == 0)
        gipat_ = true;

    selectPending_ = 1;
    SelectZone(-1, 0);
    dragging_ = false;
    GameSetWeather(0, 0.0f);   // clear
    (ei::GameData<SoundMixer*>(0x007AF760))->ResetMusic();
    {
        auto* weather = static_cast<CCloudNoise*>(gamecrt::operator_new(0x34));
        if (weather) {
            weather->Construct(0x100);
            ei::SetVtable(weather, kWeatherVtable);
        }
        weather_ = reinterpret_cast<GlobalMapWeather*>(weather);
        weather->Start();
    }
    static const int kWeather[18] = {0, 0, 10, 0xFF, 0x9B, 0, 0xE1, 0x64, 0x9B, 0, 0x7D, 0x6E, 0x69, 10, 0xC3, 0xB9,
                                     0x77, 0xF8};
    for (int i = 0; i < 18; ++i) weatherTable_[i] = kWeather[i];
    for (int i = 0; i < 18; ++i) {
        weatherIndex_ = i;
        SetWeather();
    }
    using UpdateFn = void(__thiscall*)(CUiWindow*);
    ei::VirtualSlot<UpdateFn>(vtable_, 24)(this);   // slot 24 (+0x60)
    windSound_ = interface_->PlaySound("buttons\\globalmap\\wind.wav", 1);   // buttons\globalmap\wind.wav
    if (autosave_) {
        if (ei::GameData<const int>(0x0079BE7C) != 0) AutoSaveOriginal("saveb", 1);
        autosave_ = false;
    }
    interface_->OpenHelp(0);
}

EI_REPLACE_METHOD(0x0062D350, CGlobalMap::Create);

// --- Update (0062F050) -----------------------------------------------------------------------------

namespace {

// 00600270 (not rewritten): time since the last frame in seconds, unrounded in ST0.
inline double __fastcall FrameTime(CUiWindow* w) { return WindowFrameTimeOriginal(w); }
// 006010E0 (not rewritten): the container's base update (its children).
void __fastcall UpdateChildren(CUiWindow* a0) { UiUpdateChildrenOriginal((void*)a0); }


// fmod(a, *divisor) through the game's _CIfmod (006E95AA), stored as float.
float FmodStored(float a, const float* divisor) { return static_cast<float>(libcrt::CiFmod(a, *divisor)); }

// The product the original inlines (r = b * a in its operand order), each component stored.
Quaternion TurnProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    float z = static_cast<float>(((D(b.x) * D(a.y) - D(a.x) * D(b.y)) + D(b.z) * D(a.w)) + D(a.z) * D(b.w));
    float y = static_cast<float>(((D(a.x) * D(b.z) - D(b.x) * D(a.z)) + D(b.y) * D(a.w)) + D(b.w) * D(a.y));
    float x = static_cast<float>(((D(a.z) * D(b.y) - D(b.z) * D(a.y)) + D(b.x) * D(a.w)) + D(a.x) * D(b.w));
    float w = static_cast<float>(D(b.w) * D(a.w) - DotUnrounded<Sum3::ZYX, double>(a.x, a.y, a.z, b.x, b.y, b.z));
    r.w = w;
    r.x = x;
    r.y = y;
    r.z = z;
    return r;
}

// A layer's texture frame: the fractional part of time * rate (stored).
void SetFrameFraction(CEngineObject* layer, float time, float rate) {
    double v = D(time) * D(rate);
    int whole = static_cast<int>(gamecrt::Ftol(v));
    float fraction = static_cast<float>(v - static_cast<double>(whole));
    std::memcpy(&AsUiMesh(layer)->textureOffsetU_, &fraction, 4);
}

} // namespace

void CGlobalMap::Update() {
    UpdateChildren(this);
    if (dragging_ && turning_ == 0) {
        POINT cursor;
        gameapi::GetCursorPos(&cursor);
        using MoveFn = void(__thiscall*)(void*, int, int, int);
        ei::VirtualSlot<MoveFn>(inputVtable_, 4)(InputSide(), 0, cursor.x, cursor.y);   // +0x10
    }
    MapBuilder builder(this);
    if (turning_ != 0 || dragging_) {
        // Turn the model about z.
        float turned = static_cast<float>(FrameTime(this) * static_cast<double>(turning_) * D(3.1415927f /*00742098*/) *
                                 D(0.25f /*0073C3B0*/) +
                             D(angle_));
        angle_ = turned;
        angle_ = FmodStored(turned, ei::GamePointer<const float>(0x007C8020));
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Vector3 side = {1.0f, 0.0f, 0.0f};
        Quaternion spin;
        spin.SetAngleAxis(angle_, up);
        Quaternion tilt;
        tilt.SetAxisAngle(side, static_cast<float>(D(3.1415927f /*00742098*/) * D(0.8f /*0073CCA8*/)));
        Quaternion turn = TurnProduct(spin, tilt);
        std::memcpy(&turn_, &turn, 16);
        EI_NAN_AUDIT(turn_.w);
        if (D(turn_.w) < D(0.0f /*0073B858*/)) {
            turn_.w = -turn_.w;
            turn_.x = -turn_.x;
            turn_.y = -turn_.y;
            turn_.z = -turn_.z;
        }
        Vector3 at = {position_.x, position_.y, position_.z};
        SetPosition(SceneMesh(1), builder.ModelPoint(at));
        builder.Turn(SceneMesh(1));
        builder.Turn(SceneMesh(2));
        for (int k = 3; k < 6; ++k) builder.Turn(SceneMesh(markerCount_ + k));
        for (int i = 0; i < zoneCount_; ++i) builder.Turn(SceneMesh(3 + i));
        for (int i = 0; i < questCount_; ++i) {
            CUiMesh* sprite = SceneMesh(3 + zoneCount_ + i);
            Vector3 local = quests_.data[i].position;
            SetPosition(sprite, builder.ModelPoint(local));
            builder.Turn(sprite);
            float x, y, depth;
            sprite->GetPlacement(&x, &y, &depth);
        }
        for (int i = 0; i < linkCount_; ++i) {
            CUiMesh* sprite = SceneMesh(3 + questCount_ + zoneCount_ + i);
            Vector3 local = links_.data[i].position;
            SetPosition(sprite, builder.ModelPoint(local));
            builder.Turn(sprite);
            float x, y, depth;
            sprite->GetPlacement(&x, &y, &depth);
            int bottom = static_cast<int>(gamecrt::Ftol(D(y) + D(10.0f /*0073BAAC*/)));
            int right = static_cast<int>(gamecrt::Ftol(D(x) + D(20.0f /*0073CCF8*/)));
            int top = static_cast<int>(gamecrt::Ftol(D(y) - D(25.0f /*0073E124*/)));
            int left = static_cast<int>(gamecrt::Ftol(D(x) - D(20.0f /*0073CCF8*/)));
            regions_.data[3 + i]->CallSetRect(left, top, right, bottom);
        }
    }

    // The current zone's arrow bobs over it.
    double time = FrameTime(this) + D(bobTime_);
    bobTime_ = static_cast<float>(time);
    double bob = D(0.5f /*0073B86C*/) - x87::fcos(time * D(5.0f /*0073BF34*/)) * D(0.2f /*0073BAD4*/);
    Vector3 above = {position_.x, position_.y, static_cast<float>(bob + D(position_.z))};
    SetPosition(SceneMesh(0), builder.ModelPoint(above));
    // The layers' texture frames run at their own rates.
    SetFrameFraction(sceneObjects_.data[markerCount_ + 3], bobTime_, 0.01f /*0073D09C*/);
    SetFrameFraction(sceneObjects_.data[markerCount_ + 4], bobTime_, 0.016666668f /*0073F8A0*/);
    SetFrameFraction(sceneObjects_.data[markerCount_ + 5], bobTime_, 0.033333335f /*007420E0*/);

    if (selectPending_ != 0) RefreshSelection();
    selectPending_ = 0;

    // The weather is drawn straight into texture 4's pixels: only the new renderer's memory
    // (00691D80, ticket R8), drawn into directly (the original: between a Lock and an Unlock).
    if (objects_.size > 4) {
        CDXSurface* holder = static_cast<CTexture*>(objects_.data[4])->surface_;
        auto* lock = TheDeviceLock();
        gameapi::EnterCriticalSection(lock);
        SurfacePixels pixels;
        if (DirectPixels(holder, &pixels)) {   // (always: the texture has memory)
            float seconds = static_cast<float>(FrameTime(this));
            reinterpret_cast<CCloudNoise*>(weather_)->Draw(reinterpret_cast<std::uint16_t*>(pixels.bits), seconds);
        }
        gameapi::LeaveCriticalSection(lock);
    }
    TheCursor()->ChangeShape(dragging_ ? 0x16 : 0);
}

EI_REPLACE_METHOD(0x0062F050, CGlobalMap::Update);

// --- 005F67A0: the constructor --------------------------------------------------------------------

namespace {

// A CArray's constructor body, inlined by the original: the instantiation's vtable, then the
// storage, grow step, capacity and size cleared in that order.
template <typename Array>
void EmptyArray(Array* array, std::uintptr_t vtableAddress) {
    array->vtable = ei::GamePointer<void>(vtableAddress);
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
}
} // namespace

CGlobalMap* CGlobalMap::Construct(CInterface3D* ui) {
    reinterpret_cast<CUiContainer*>(this)->Construct(ui);
    ::new (&allodTitle_) mfc::CString;
    EmptyArray(&zones_, 0x00740BFC);
    EmptyArray(&links_, 0x00740BF0);
    EmptyArray(&quests_, 0x00740BE4);
    path_.Start() = nullptr;
    path_.Finish() = nullptr;
    path_.EndOfStorage() = nullptr;   // the allocator proxy (005FA0C0)
    ::new (&target_) mfc::CString;
    // The tree (005FA100: the header from the pool; 005FA0E0 empties it) keeps the first byte of
    // the original's compare object, the argument slot holding `ui`.
    arrivals_.Header() = static_cast<stlp::map<int, float>::Node*>(stlp::allocate(sizeof(stlp::map<int, float>::Node)));
    arrivals_.Count() = 0;
    arrivals_.Compare() = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(ui));
    arrivals_.EmptyInitialize();
    turn_.w = 1.0f;   // the identity turn
    turn_.x = 0.0f;
    turn_.y = 0.0f;
    turn_.z = 0.0f;
    ::new (&shownName_) mfc::CString;
    vtable_ = ei::GamePointer<void*>(0x00740B3C);
    inputVtable_ = ei::GamePointer<void*>(0x00740B08);
    return this;
}
EI_REWRITTEN(0x005F67A0, CGlobalMap::Construct);

// --- 005F6920: the destructor body ---------------------------------------------------------------

namespace {
// A CArray's destructor: its vtable back, each entry's strings destroyed (title, then name:
// 005FBDD0 / 005FBD80 the places', 005FBD30 the zones'), the storage freed.
template <typename Array>
void DestroyArray(Array* array, std::uintptr_t vtableAddress) {
    array->vtable = ei::GamePointer<void>(vtableAddress);
    if (!array->data) return;
    int size = array->size;
    for (int i = 0; i < size; ++i) {
        CStringDestroy(&array->data[i].title);
        CStringDestroy(&array->data[i].name);
    }
    gamecrt::operator_delete(array->data);
}
} // namespace

// 00600BA0: the container base's destructor body.
void __fastcall GlobalMapDestroy(CGlobalMap* self) {
    CStringDestroy(&self->shownName_);
    self->arrivals_.Destroy();   // 005FB7D0 on the root, 005F9980
    CStringDestroy(&self->target_);
    stlp::deallocate(self->path_.Start(), static_cast<std::size_t>(self->path_.EndOfStorage() - self->path_.Start()) *
                                              sizeof(GlobalMapStep));   // 005FA7E0
    DestroyArray(&self->quests_, 0x00740BE4);
    DestroyArray(&self->links_, 0x00740BF0);
    DestroyArray(&self->zones_, 0x00740BFC);
    CStringDestroy(&self->allodTitle_);
    DestroyContainerBody(self);
}
EI_REWRITTEN(0x005F6920, GlobalMapDestroy);

// --- 00631CC0: the zone array's SetAtGrow --------------------------------------------------------

void GlobalMapZoneElements::Construct(GlobalMapZone* zones, int count) {
    std::memset(static_cast<void*>(zones), 0, static_cast<std::size_t>(count) * sizeof(GlobalMapZone));
    for (int i = 0; i < count; ++i) {
        new (&zones[i].name) mfc::CString();
        new (&zones[i].title) mfc::CString();
    }
}

void GlobalMapZoneElements::Destruct(GlobalMapZone* zones, int count) {
    for (int i = 0; i < count; ++i) {
        zones[i].title.~CString();
        zones[i].name.~CString();
    }
}

void __fastcall GlobalMapZonesSetAtGrow(GlobalMapZoneArray* zones, void*, int index, const GlobalMapZone* zone) {
    if (index >= zones->size) zones->SetSize(index + 1);
    GlobalMapZone& slot = zones->data[index];
    slot.state = zone->state;
    slot.link = zone->link;
    slot.name = zone->name;
    slot.title = zone->title;
}
EI_REWRITTEN(0x00631CC0, GlobalMapZonesSetAtGrow);
