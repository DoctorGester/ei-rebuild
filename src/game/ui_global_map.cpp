// The global map's Create (slot 25 of 00740B3C, 0062D350): the island model of the local player's
// allod, its zones, the quests the player holds and the links to the zones the player may enter,
// the clock text, the camp and turn buttons, the weather. See docs/notes/ui.md ("Camp, global map, hero
// screens").
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float. Constants are read from the exe's memory,
// as the original does.
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
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <concepts>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <bit>

using console::CStringArray;

namespace {

inline double D(float f) { return f; }

float Bits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Bits(T) = delete;

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The game's _ftol (006E853C): truncation of the value in ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// --- Game data ----------------------------------------------------------------------------------

// An allod of the map database (AllodRecord, object_tables.h): its global map figures and zones.
struct AllodView {
    char* name;                      // +0x00 CString
    char* arrow;                     // +0x04 the current zone's marker
    char* ball;                      // +0x08 its base
    char* questMarker;               // +0x0C
    char* island;                    // +0x10 the island model
    char* armor;                     // +0x14
    std::uint8_t unused_18[4];
    int* zones;                      // +0x1C indices into the map database's maps
    int zoneCount;                   // +0x20
    std::uint8_t unused_24[0x2C - 0x24];
};
static_assert(sizeof(AllodView) == 0x2C);

CMapDatabase* MapDatabase() { return reinterpret_cast<CMapDatabase*>(0x0079BF00); }
// A zone of the map database: its figure is its marker on the global map (may be empty), its
// kind 0 a game zone and 2 a way between zones here.
MapRecord* FindZone(const char* name) { return MapDatabase()->FindMap(name); }
MapRecord* ZoneAt(int index) { return MapDatabase()->maps.data + index; }

bool NonEmpty(const char* cstring) { return reinterpret_cast<const int*>(cstring)[-2] != 0; }

// --- Not rewritten ------------------------------------------------------------------------------

// The global map's own helpers (thiscall on the map).
struct MapScreen {
    mfc::CString* AllodTitle(mfc::CString* out, const char* allod);     // 00631110
    Vector3* ZonePosition(Vector3* out, const char* zone);              // 00630E00
    mfc::CString* ZoneTitle(mfc::CString* out, const char* zone);       // 00631030
    mfc::CString* QuestTitle(mfc::CString* out, const char* quest);     // 00630F50
    Vector3* QuestPosition(Vector3* out, const char* quest);            // 00630CC0
    void SetWeather();                                                   // 0062EE90 (entry +0x260)
};
__declspec(naked) mfc::CString* MapScreen::AllodTitle(mfc::CString*, const char*) EI_ORIGINAL(0x00631110)
__declspec(naked) Vector3* MapScreen::ZonePosition(Vector3*, const char*) EI_ORIGINAL(0x00630E00)
__declspec(naked) mfc::CString* MapScreen::ZoneTitle(mfc::CString*, const char*) EI_ORIGINAL(0x00631030)
__declspec(naked) mfc::CString* MapScreen::QuestTitle(mfc::CString*, const char*) EI_ORIGINAL(0x00630F50)
__declspec(naked) Vector3* MapScreen::QuestPosition(Vector3*, const char*) EI_ORIGINAL(0x00630CC0)
__declspec(naked) void MapScreen::SetWeather() EI_ORIGINAL(0x0062EE90)

// The marker arrays (MFC CArray instantiations) and their elements' assignment. The zones'
// SetAtGrow (00631CC0) is rewritten below; called through its address so its wrapper runs.
__declspec(naked) void __fastcall ZonesSetAtGrowAt(GlobalMapZoneArray*, void*, int, const GlobalMapZone*) EI_ORIGINAL(0x00631CC0)
// The links' and the quests' SetSize (00631E70 / 00631FF0, thiscall, RET 8) and their elements'
// assignment (GlobalMapPlace::Assign / AssignCopy, 00632490 / 006324E0, RET 4).
__declspec(naked) void __fastcall LinksSetSize(mfc::CArray<GlobalMapPlace>*, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x00631E70)
__declspec(naked) void __fastcall QuestsSetSize(mfc::CArray<GlobalMapPlace>*, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x00631FF0)
__declspec(naked) void __fastcall AssignLink(GlobalMapPlace*, void*, const GlobalMapPlace& /*from*/)
    EI_ORIGINAL(0x00632490)
__declspec(naked) void __fastcall AssignQuest(GlobalMapPlace*, void*, const GlobalMapPlace& /*from*/)
    EI_ORIGINAL(0x006324E0)

// 0046A4B0 (QuaternionRotate, map_spec.cpp; thiscall on the quaternion, RET 8): `out` = the vector
// `v` turned by the quaternion.
__declspec(naked) Vector3* __fastcall Rotate(const Quaternion* /*quaternion*/, void*, Vector3* /*out*/, const Vector3* /*v*/)
    EI_ORIGINAL(0x0046A4B0)

// Sprites (CUiMesh) beyond ui.h.
struct SpriteOps : SpritePositionOriginal {
    void SetScale(float scale);                         // 005E7060
};
void SpriteOps::SetScale(float a0) { SpriteSetScaleOriginal((void*)this, nullptr, std::bit_cast<std::uint32_t>(a0)); }
SpriteOps* Ops(CUiMesh* sprite) { return reinterpret_cast<SpriteOps*>(sprite); }

// Drawing text on a surface (005FDE20 is Rebirth-patched).
int SurfaceText(CUiSurface* surface, const char* text, RECT* rect, int font, std::uint32_t colour, int format, int flag) {
    return SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, flag);
}

// The local player's variables, copied by name prefix into a hash_map<CString, float> (constructed
// with 100 buckets by 00581790).
struct VariableIterator {
    NamedFloatTable::Node* node;
    NamedFloatTable* table;
    NamedFloatTable::Node* NextBucket();                // 005E0DB0
};
NamedFloatTable::Node* VariableIterator::NextBucket() { return (NamedFloatTable::Node*)VariableIteratorNextOriginal((void*)this); }

// A local hash_map<CString, float>, freed the way the original's inlined destructor does: every
// node's key released and the node returned to the 16-byte free list, then the bucket storage.
class VariableTable {
public:
    VariableTable() { NamedTableConstructOriginal(&table_); }
    ~VariableTable() {
        std::size_t n = static_cast<std::size_t>(table_.finish - table_.buckets);
        for (std::size_t b = 0; b < n; ++b) {
            NamedFloatTable::Node* node = table_.buckets[b];
            while (node) {
                NamedFloatTable::Node* next = node->next;
                reinterpret_cast<mfc::CString*>(&node->key)->~CString();
                stlp::deallocate(node, sizeof(NamedFloatTable::Node));
                node = next;
            }
            table_.buckets[b] = nullptr;
        }
        table_.count = 0;
        if (table_.buckets)
            stlp::deallocate(table_.buckets,
                             static_cast<std::size_t>(table_.endOfStorage - table_.buckets) * sizeof(void*));
    }
    NamedFloatTable* Table() { return &table_; }

private:
    NamedFloatTable table_;
};

// The rest (not rewritten).
struct SoundMixer {
    void ResetMusic() { MusicSystemStopOriginal(reinterpret_cast<CMusicSystem*>(this)); }   // 006B4F70 (the object at *007AF760)
};
struct WeatherObject {
    void Construct(int size);                           // 00499DC0
    void Start();                                       // 00499EE0
};
__declspec(naked) void WeatherObject::Construct(int) EI_ORIGINAL(0x00499DC0)
__declspec(naked) void WeatherObject::Start() EI_ORIGINAL(0x00499EE0)
// The scene renderer (007AF060, what 00500960 returns).
CSceneRenderer* Renderer() { return reinterpret_cast<CSceneRenderer*>(0x007AF060); }
// 006570E0: saves the game into saves\<name> (ECX name, DL flag).

// --- Sprite fields set directly -------------------------------------------------------------------

// Float fields are written with their bits, as the original's immediate stores do.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, 4); }
void SetTexture(CUiMesh* sprite, CEngineObject* texture) { sprite->texture_ = texture; }
void SetPlain(CUiMesh* sprite) { sprite->flags_ |= 0x100; }
void SetPosition(CUiMesh* sprite, const Vector3& p) { std::memcpy(&sprite->position_, &p, 12); }
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
        float x = static_cast<float>((D(map_->centreX_) * D(GameFloat(0x0074015C)) - D(GameFloat(0x0073B84C))) *
                        D(GameFloat(0x0074209C)) * D(depth));
        float y = static_cast<float>((D(map_->centreY_) * D(GameFloat(0x0074015C)) - D(GameFloat(0x0073C524))) *
                        D(GameFloat(0x0074209C)) * D(depth));
        return Vector3{x, y, depth};
    }
    // A point of the map model (`local`) in camera space: the centre plus the turned point.
    Vector3 ModelPoint(const Vector3& local) const {
        Vector3 centre = Centre();
        Vector3 turned;
        Rotate(&map_->turn_, nullptr, &turned, &local);
        Vector3 out;
        AddVectors(&out, centre, turned);
        return out;
    }
    void Turn(CUiMesh* sprite) const { std::memcpy(&sprite->orientation_, &map_->turn_, 16); }
    CEngineObject* Texture(int index) const { return map_->objects_.data[index]; }

    // A marker of the map model: the figure at `local`, scaled, turned, white; returns the sprite.
    CUiMesh* Marker(const char* figure, const Vector3& local, float scale) const {
        CUiMesh* sprite = AddFigure(map_, figure);
        SetPosition(sprite, ModelPoint(local));
        SetTexture(sprite, Texture(1));
        Ops(sprite)->SetScale(scale);
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
        Rotate(&map_->turn_, nullptr, &turned, &local);
        Vector3 p;
        p.Construct(static_cast<float>(D(centre.x) + D(turned.x)), static_cast<float>(D(centre.y) + D(turned.y)),
                    static_cast<float>(D(centre.z) + D(turned.z)));
        SetPosition(sprite, p);
        return sprite;
    }

private:
    CGlobalMap* map_;
};

MapScreen* Screen(CGlobalMap* map) { return reinterpret_cast<MapScreen*>(map); }

// Element templates with real strings (layouts of GlobalMapZone / GlobalMapPlace).
struct ZoneTemplate {
    int state = 0;
    int link = 0;
    mfc::CString name;
    mfc::CString title;
    const GlobalMapZone& Get() const { return *reinterpret_cast<const GlobalMapZone*>(this); }
};
static_assert(sizeof(ZoneTemplate) == sizeof(GlobalMapZone));
struct PlaceTemplate {
    int zone = 0;
    Vector3 position{0.0f, 0.0f, 0.0f};
    mfc::CString name;
    mfc::CString title;
    const GlobalMapPlace& Get() const { return *reinterpret_cast<const GlobalMapPlace*>(this); }
};
static_assert(sizeof(PlaceTemplate) == sizeof(GlobalMapPlace));

void AddTipRegion(CUiWindow* window, int left, int top, int right, int bottom, int tip) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    region->SetTip(tip, 1, 0);
}

// A region over a marker sprite (its layout position): 20 to each side, 25 above, 10 below.
void AddMarkerRegion(CGlobalMap* map, CUiMesh* sprite) {
    float x, y, depth;
    Ops(sprite)->Position(&x, &y, &depth);
    int bottom = GameFtol(D(y) + D(GameFloat(0x0073BAAC)));
    int right = GameFtol(D(x) + D(GameFloat(0x0073CCF8)));
    int top = GameFtol(D(y) - D(GameFloat(0x0073E124)));
    int left = GameFtol(D(x) - D(GameFloat(0x0073CCF8)));
    map->AddRegion()->CallSetRect(left, top, right, bottom);
}

} // namespace

void CGlobalMap::Create() {
    autosave_ = false;
    if (*reinterpret_cast<const int*>(0x007AFE1C) != 0)
        GameSaveStream()->Resize(0);
    else if (reinterpret_cast<LocalViewData*>(WorldClient()->LocalView())->player->lastScreen != 5)
        autosave_ = true;   // not back from the camp
    depth_ = Bits(0x41200000);     // 10
    centreX_ = Bits(0x43C80000);   // 400
    centreY_ = Bits(0x438C0000);   // 280
    turning_ = 0;
    bobTime_ = 0.0f;

    MapRecord* current;
    {
        alignas(4) std::uint8_t raw[4];
        mfc::CString* name = LocalPlayerName(reinterpret_cast<mfc::CString*>(raw));
        current = FindZone(*name);
        name->~CString();
    }
    AllodView* allod = reinterpret_cast<AllodView*>(MapDatabase()->FindAllod(current->allod));
    {
        alignas(4) std::uint8_t raw[4];
        mfc::CString* title = Screen(this)->AllodTitle(reinterpret_cast<mfc::CString*>(raw), allod->name);
        *reinterpret_cast<mfc::CString*>(&allodTitle_) = *title;
        title->~CString();
    }
    {
        Vector3 position;
        Screen(this)->ZonePosition(&position, current->name);
        std::memcpy(&position_, &position, 12);
    }
    const mfc::CString& allodName = *reinterpret_cast<const mfc::CString*>(&allod->name);
    LoadTexture(mfc::operator+(allodName, game::Text(0x0079415C)), 3, 0);   // <allod>Map
    LoadTexture(mfc::operator+(allodName, game::Text(0x00794158)), 3, 0);   // <allod>Sys
    LoadTexture(mfc::operator+(allodName, game::Text(0x00794150)), 3, 0);   // <allod>MapG
    LoadTexture(game::Text(0x00793380), 3, 0);                             // "saveload"
    CreateTexture(0x100, 0x100, 0x21);
    angle_ = 0.0f;
    {
        Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion q;
        q.SetAxisAngle(axis, static_cast<float>(D(GameFloat(0x00742098)) * D(GameFloat(0x0073CCA8))));   // pi * 0.8
        std::memcpy(&turn_, &q, 16);
    }
    MapBuilder builder(this);

    // The current zone: its arrow above it, its base on it, the island.
    {
        Vector3 above = {position_.x, position_.y, static_cast<float>(D(position_.z) + D(GameFloat(0x0073CDB8)))};
        builder.Marker(allod->arrow, above, 1.0f);
        Vector3 at = {position_.x, position_.y, position_.z};
        builder.Marker(allod->ball, at, Bits(0x3F4CCCCD));   // 0.8
        CUiMesh* island = AddFigure(this, allod->island);
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
    ZoneTemplate zoneTemplate;
    PlaceTemplate linkTemplate;
    PlaceTemplate questTemplate;
    linkCount_ = 0;
    questCount_ = 0;
    zoneCount_ = 0;

    // The game zones of the allod that have a marker, with the player's "z.<zone>" state.
    for (int i = 0; i < allod->zoneCount; ++i) {
        MapRecord* zone = ZoneAt(allod->zones[i]);
        if (zone->kind != 0 || !NonEmpty(zone->figure)) continue;
        ZonesSetAtGrowAt(&zones_, nullptr, zones_.size, &zoneTemplate.Get());
        variable.Format(game::Text(0x00794148), zone->name);   // "z.%s"
        zones_.data[zoneCount_].link = -1;
        zones_.data[zoneCount_].state = GameFtol(D(LocalPlayerVariable(variable)));
        *reinterpret_cast<mfc::CString*>(&zones_.data[zoneCount_].name) = *reinterpret_cast<mfc::CString*>(&zone->name);
        {
            alignas(4) std::uint8_t raw[4];
            mfc::CString* title = Screen(this)->ZoneTitle(reinterpret_cast<mfc::CString*>(raw), zone->name);
            *reinterpret_cast<mfc::CString*>(&zones_.data[zoneCount_].title) = *title;
            title->~CString();
        }
        CUiMesh* sprite = AddFigure(this, zone->figure);
        sprite->Place(centreX_, centreY_, depth_);
        builder.Turn(sprite);
        ++zoneCount_;
    }

    // The quests the player holds in each zone ("q.<zone>.<quest>" = 1).
    for (int i = 0; i < zoneCount_; ++i) {
        variable.Format(game::Text(0x00794140), zones_.data[i].name);   // "q.%s"
        reinterpret_cast<LocalPlayerOriginal*>(0x007AFEA5)->CopyVariables(variable, vars.Table());
        VariableIterator it;
        FloatTableBeginOriginal(vars.Table(), nullptr, &it);
        while (NamedFloatTable::Node* node = it.node) {
            std::uint32_t value;
            std::memcpy(&value, &node->value, 4);
            if (value == 0x3F800000) {
                StringSplit(node->key, &parts, '.');
                if (parts.GetSize() == 3) {
                    int n = questCount_;
                    QuestsSetSize(&quests_, nullptr, quests_.size + 1, -1);
                    AssignQuest(&quests_.data[quests_.size - 1], nullptr, questTemplate.Get());
                    GlobalMapPlace& quest = quests_.data[n];
                    *reinterpret_cast<mfc::CString*>(&quest.name) = parts[2];
                    quests_.data[n].zone = i;
                    {
                        alignas(4) std::uint8_t raw[4];
                        mfc::CString* title = Screen(this)->QuestTitle(reinterpret_cast<mfc::CString*>(raw), parts[2]);
                        *reinterpret_cast<mfc::CString*>(&quests_.data[n].title) = *title;
                        title->~CString();
                    }
                    {
                        Vector3 position;
                        Screen(this)->QuestPosition(&position, parts[2]);
                        std::memcpy(&quests_.data[n].position, &position, 12);
                    }
                    CUiMesh* sprite = AddFigure(this, allod->questMarker);
                    Vector3 local = quests_.data[n].position;
                    SetPosition(sprite, builder.ModelPoint(local));
                    SetTexture(sprite, builder.Texture(1));
                    Ops(sprite)->SetScale(GameFloat(0x007420D0));
                    builder.Turn(sprite);
                    SetWhite(sprite);
                    float x, y, depth;
                    Ops(sprite)->Position(&x, &y, &depth);
                    ++questCount_;
                }
            }
            NamedFloatTable::Node* next = node->next;
            if (!next) next = it.NextBucket();
            it.node = next;
        }
    }

    // The ways out of each zone the player may take ("z.<way>" = 2), with a region each.
    for (int i = 0; i < zoneCount_; ++i) {
        MapRecord* zone = FindZone(zones_.data[i].name);
        for (int e = 0; e < zone->exits.size; ++e) {
            MapRecord* way = FindZone(zone->exits.data[e].target);
            if (!way) continue;
            variable.Format(game::Text(0x00794148), way->name);   // "z.%s"
            if (way->kind != 2) continue;
            // FCOMP / TEST AH,0x40: equal to 2, or unordered.
            double state = D(LocalPlayerVariable(variable));
            double two = D(GameFloat(0x0073CDB8));
            if (state < two || state > two) continue;
            if (!NonEmpty(way->figure)) continue;
            if (zones_.data[i].state == 2) zones_.data[i].link = linkCount_;
            int n = linkCount_;
            LinksSetSize(&links_, nullptr, links_.size + 1, -1);
            AssignLink(&links_.data[links_.size - 1], nullptr, linkTemplate.Get());
            links_.data[n].zone = i;
            *reinterpret_cast<mfc::CString*>(&links_.data[n].name) = *reinterpret_cast<mfc::CString*>(&way->name);
            {
                alignas(4) std::uint8_t raw[4];
                mfc::CString* title = Screen(this)->ZoneTitle(reinterpret_cast<mfc::CString*>(raw), way->name);
                *reinterpret_cast<mfc::CString*>(&links_.data[n].title) = *title;
                title->~CString();
            }
            {
                Vector3 position;
                Screen(this)->ZonePosition(&position, way->name);
                std::memcpy(&links_.data[n].position, &position, 12);
            }
            CUiMesh* sprite = AddFigure(this, way->figure);
            Vector3 local = links_.data[n].position;
            SetPosition(sprite, builder.ModelPoint(local));
            SetTexture(sprite, builder.Texture(1));
            Ops(sprite)->SetScale(GameFloat(0x007420D0));
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
        shadow->plainColour_ = 0xA08200C8;
        SetBits(shadow->vOffset_, 0x3E99999A);   // 0.3
        SetPlain(shadow);
        CUiMesh* haze = builder.Layer(0, 0x100, 0x100, Bits(0x3F333333));    // 0.7
        haze->plainColour_ = 0xF0B400FF;
        SetBits(haze->vOffset_, 0x3F19999A);     // 0.6
        SetPlain(haze);
        CUiMesh* glow = builder.Layer(0, 0x100, 0x100, Bits(0x40000000));    // 2.0
        glow->plainColour_ = 0x4BFFFFFF;
        glow->visible_ = 0;
        SetPlain(glow);
    }
    Renderer()->fogColour_ = 0xFF3C005A;

    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        backdrop_ = memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, 400, 300, 5, 1) : nullptr;
        backdrop_->SetRect(0, 0, 800, 600);
    }
    {
        CUiMesh* shade = AddSprite(0);
        SetTexture(shade, nullptr);
        SetPlain(shade);
        shade->plainColour_ = 0xA0000000;
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
        shade->plainColour_ = 0xA0000000;
        SetPlain(shade);
        CUiMesh* frame = AddSprite(0);
        frame->SetFrame(3.0f, 100.0f, 15.0f, 15.0f, 175.0f, 75.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
        SetTexture(frame, builder.Texture(3));
    }

    // The clock: "<save_day>  <day>,  <h>:<mm>" under the allod's title.
    int minutes = GameFtol(D(LocalPlayerVariable(game::Text(0x007420A4))) * D(GameFloat(0x0073F090)));   // gtime * 60
    int day = minutes / 1440;
    minutes -= day * 1440;
    int hour = minutes / 60;
    minutes -= hour * 60;
    mfc::CString clock;
    mfc::CString dayText;
    (*reinterpret_cast<CTextManager**>(0x007AF788))->GetString(game::Text(0x00794134), &dayText);   // "save_day"
    clock.Format(game::Text(0x00794120), static_cast<const char*>(dayText), day + 1, hour, minutes);
    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        CUiSurface* text = memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 20, 20, 170, 70, 5, 1)
                                  : nullptr;
        AddSurface(text);
        SurfaceGetDC(text);
        RECT line1 = {0, 5, 150, 25};
        SurfaceText(text, allodTitle_, &line1, 2, 0xA7D7E4, 0x25, 1);
        RECT line2 = {0, 25, 150, 45};
        SurfaceText(text, clock, &line2, 2, 0xA7D7E4, 0x25, 1);
        SurfaceReleaseDC(text);
    }

    // The camp button (hidden with "i.nocamp" = 1) and the two turn buttons.
    {
        CUiMesh* camp = AddSprite(0);
        camp->SetRectangle(3.0f, 100.0f, 15.0f, 525.0f, 107.0f, 585.0f, 2, 0x97, 0x5E, 0xD3);
        SetTexture(camp, builder.Texture(3));
        noCamp_ = GameFtol(D(LocalPlayerVariable(game::Text(0x00794114))));   // "i.nocamp"
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
    if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(allod->name),
                        reinterpret_cast<const unsigned char*>(game::Text(0x00790244))) == 0) {   // "ingos"
        fog_[0] = Bits(0x3EBE0000);
        fog_[1] = Bits(0x3F020000);
        fog_[2] = Bits(0x3F200000);
    } else if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(allod->name),
                               reinterpret_cast<const unsigned char*>(game::Text(0x00790238))) == 0) {   // "suslanger"
        fog_[0] = Bits(0x3EC80000);
        fog_[1] = Bits(0x3E480000);
        fog_[2] = Bits(0x3DB00000);
    }
    if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(allod->name),
                        reinterpret_cast<const unsigned char*>(game::Text(0x0079014C))) == 0)   // "gipat"
        gipat_ = true;

    selectPending_ = 1;
    GlobalMapSelectZoneOriginal(this, nullptr, -1, 0);
    dragging_ = false;
    GameSetWeatherOriginal(reinterpret_cast<void*>(0x007AFEA6), nullptr, 0, 0.0f);   // clear
    (*reinterpret_cast<SoundMixer**>(0x007AF760))->ResetMusic();
    {
        WeatherObject* weather = static_cast<WeatherObject*>(gamecrt::operator_new(0x34));
        if (weather) {
            weather->Construct(0x100);
            *reinterpret_cast<std::uintptr_t*>(weather) = 0x007420D4;
        }
        weather_ = reinterpret_cast<GlobalMapWeather*>(weather);
        weather->Start();
    }
    static const int kWeather[18] = {0, 0, 10, 0xFF, 0x9B, 0, 0xE1, 0x64, 0x9B, 0, 0x7D, 0x6E, 0x69, 10, 0xC3, 0xB9,
                                     0x77, 0xF8};
    for (int i = 0; i < 18; ++i) weatherTable_[i] = kWeather[i];
    for (int i = 0; i < 18; ++i) {
        weatherIndex_ = i;
        Screen(this)->SetWeather();
    }
    using UpdateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<UpdateFn>(vtable_[24])(this);   // slot 24 (+0x60)
    windSound_ = interface_->PlaySound(game::Text(0x007940F8), 1);   // buttons\globalmap\wind.wav
    if (autosave_) {
        if (*reinterpret_cast<const int*>(0x0079BE7C) != 0) AutoSaveOriginal(game::Text(0x007940F0), 1);   // "saveb"
        autosave_ = false;
    }
    interface_->ShowHelp(0);
}

EI_REPLACE_METHOD(0x0062D350, CGlobalMap::Create);

// --- Update (0062F050) -----------------------------------------------------------------------------

namespace {

// 00600270 (not rewritten): time since the last frame in seconds, unrounded in ST0.
inline double __fastcall FrameTime(CUiWindow* w) { return WindowFrameTimeOriginal(w); }
// 006010E0 (not rewritten): the container's base update (its children).
void __fastcall UpdateChildren(CUiWindow* a0) { UiUpdateChildrenOriginal((void*)a0); }

// CGlobalMap::RefreshSelection (00630670, thiscall).
__declspec(naked) void __fastcall RefreshSelectionOriginal(CGlobalMap*) EI_ORIGINAL(0x00630670)

// The weather (+0x214, a CCloudNoise): 00499F10 (RET 8) draws into locked surface memory.
__declspec(naked) void __fastcall WeatherDrawOriginal(GlobalMapWeather*, void*, void* /*pixels*/, float /*seconds*/)
    EI_ORIGINAL(0x00499F10)

// fmod(a, *divisor) through the game's _CIfmod (006E95AA), stored as float.
float FmodStored(float a, const float* divisor) {
    float result;
    __asm {
        fld a
        mov eax, divisor
        fld dword ptr [eax]
        mov eax, 0x006E95AA
        call eax
        fstp result
    }
    return result;
}

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
    int whole = GameFtol(v);
    float fraction = static_cast<float>(v - static_cast<double>(whole));
    std::memcpy(&reinterpret_cast<CUiMesh*>(layer)->uOffset_, &fraction, 4);
}

} // namespace

void CGlobalMap::Update() {
    UpdateChildren(this);
    if (dragging_ && turning_ == 0) {
        POINT cursor;
        gameapi::GetCursorPos(&cursor);
        using MoveFn = void(__thiscall*)(void*, int, int, int);
        reinterpret_cast<MoveFn>(inputVtable_[4])(InputSide(), 0, cursor.x, cursor.y);   // +0x10
    }
    MapBuilder builder(this);
    if (turning_ != 0 || dragging_) {
        // Turn the model about z.
        float turned = static_cast<float>(FrameTime(this) * static_cast<double>(turning_) * D(GameFloat(0x00742098)) *
                                 D(GameFloat(0x0073C3B0)) +
                             D(angle_));
        angle_ = turned;
        angle_ = FmodStored(turned, reinterpret_cast<const float*>(0x007C8020));
        Vector3 up = {0.0f, 0.0f, 1.0f};
        Vector3 side = {1.0f, 0.0f, 0.0f};
        Quaternion spin;
        spin.SetAngleAxis(angle_, up);
        Quaternion tilt;
        tilt.SetAxisAngle(side, static_cast<float>(D(GameFloat(0x00742098)) * D(GameFloat(0x0073CCA8))));
        Quaternion turn = TurnProduct(spin, tilt);
        std::memcpy(&turn_, &turn, 16);
        if (!(D(turn_.w) >= D(GameFloat(0x0073B858)))) {   // FCOMP; TEST AH,1: below 0 or unordered
            turn_.w = -turn_.w;
            turn_.x = -turn_.x;
            turn_.y = -turn_.y;
            turn_.z = -turn_.z;
        }
        CEngineObject** sprites = sceneObjects_.data;
        Vector3 at = {position_.x, position_.y, position_.z};
        SetPosition(reinterpret_cast<CUiMesh*>(sprites[1]), builder.ModelPoint(at));
        builder.Turn(reinterpret_cast<CUiMesh*>(sprites[1]));
        builder.Turn(reinterpret_cast<CUiMesh*>(sprites[2]));
        for (int k = 3; k < 6; ++k) builder.Turn(reinterpret_cast<CUiMesh*>(sprites[markerCount_ + k]));
        for (int i = 0; i < zoneCount_; ++i) builder.Turn(reinterpret_cast<CUiMesh*>(sceneObjects_.data[3 + i]));
        for (int i = 0; i < questCount_; ++i) {
            CUiMesh* sprite = reinterpret_cast<CUiMesh*>(sceneObjects_.data[3 + zoneCount_ + i]);
            Vector3 local = quests_.data[i].position;
            SetPosition(sprite, builder.ModelPoint(local));
            builder.Turn(sprite);
            float x, y, depth;
            Ops(sprite)->Position(&x, &y, &depth);
        }
        for (int i = 0; i < linkCount_; ++i) {
            CUiMesh* sprite = reinterpret_cast<CUiMesh*>(sceneObjects_.data[3 + questCount_ + zoneCount_ + i]);
            Vector3 local = links_.data[i].position;
            SetPosition(sprite, builder.ModelPoint(local));
            builder.Turn(sprite);
            float x, y, depth;
            Ops(sprite)->Position(&x, &y, &depth);
            int bottom = GameFtol(D(y) + D(GameFloat(0x0073BAAC)));
            int right = GameFtol(D(x) + D(GameFloat(0x0073CCF8)));
            int top = GameFtol(D(y) - D(GameFloat(0x0073E124)));
            int left = GameFtol(D(x) - D(GameFloat(0x0073CCF8)));
            regions_.data[3 + i]->CallSetRect(left, top, right, bottom);
        }
    }

    // The current zone's arrow bobs over it.
    double time = FrameTime(this) + D(bobTime_);
    bobTime_ = static_cast<float>(time);
    double bob = D(GameFloat(0x0073B86C)) - x87::fcos(time * D(GameFloat(0x0073BF34))) * D(GameFloat(0x0073BAD4));
    Vector3 above = {position_.x, position_.y, static_cast<float>(bob + D(position_.z))};
    SetPosition(reinterpret_cast<CUiMesh*>(sceneObjects_.data[0]), builder.ModelPoint(above));
    // The layers' texture frames run at their own rates.
    SetFrameFraction(sceneObjects_.data[markerCount_ + 3], bobTime_, GameFloat(0x0073D09C));
    SetFrameFraction(sceneObjects_.data[markerCount_ + 4], bobTime_, GameFloat(0x0073F8A0));
    SetFrameFraction(sceneObjects_.data[markerCount_ + 5], bobTime_, GameFloat(0x007420E0));

    if (selectPending_ != 0) RefreshSelectionOriginal(this);
    selectPending_ = 0;

    // The weather is drawn straight into texture 4's surface; in the switch-over that texture has
    // only the new renderer's memory (00691D80, ticket R8), drawn into directly (no Lock / Unlock).
    if (objects_.size > 4) {
        CDXSurface* holder = static_cast<CTexture*>(objects_.data[4])->surface_;
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
        gameapi::EnterCriticalSection(lock);
        std::uint32_t desc[0x1F] = {};
        desc[0] = 0x7C;
        bool direct = DirectPixels(holder, desc);
        if (!direct) {
            long hr = static_cast<IDirectDrawSurface7*>(holder->surface)
                          ->Lock(nullptr, reinterpret_cast<DDSURFACEDESC2*>(desc), DDLOCK_WAIT, nullptr);
            holder->result = hr;
            if (hr != 0) FatalDirectX(hr, game::Text(0x0078E30C));   // "Can't lock surface"
        }
        float seconds = static_cast<float>(FrameTime(this));
        WeatherDrawOriginal(weather_, nullptr, reinterpret_cast<void*>(desc[9]), seconds);   // lpSurface
        gameapi::LeaveCriticalSection(lock);
        if (!direct) {
            long hr = static_cast<IDirectDrawSurface7*>(holder->surface)->Unlock(nullptr);
            holder->result = hr;
            if (hr != 0) FatalDirectX(hr, game::Text(0x0078E2F4));   // "Can't unlock surface"
        }
    }
    CursorSetShapeOriginal(reinterpret_cast<void*>(0x007C7BD8), nullptr, dragging_ ? 0x16 : 0);
}

EI_REPLACE_METHOD(0x0062F050, CGlobalMap::Update);

// --- 005F67A0: the constructor --------------------------------------------------------------------

namespace {
// The STLport containers' constructors (thiscall): 005FA0C0 (compare, allocator; RET 8) on the
// way vector's end of storage, 005FA100 (allocator; RET 4: the tree header node) and 005FA0E0
// (empties it) on the arrivals tree.
__declspec(naked) void __fastcall ConstructPair(void*, void*, const void*, const void*) EI_ORIGINAL(0x005FA0C0)
__declspec(naked) void __fastcall ConstructTree(void*, void*, const void*) EI_ORIGINAL(0x005FA100)
__declspec(naked) void __fastcall EmptyTree(void*) EI_ORIGINAL(0x005FA0E0)

char* EmptyCString() { return *reinterpret_cast<char* const*>(0x00796E48); }

// A CArray's constructor body, inlined by the original: the instantiation's vtable, then the
// storage, grow step, capacity and size cleared in that order.
template <typename Array>
void EmptyArray(Array* array, std::uintptr_t vtable) {
    array->vtable = reinterpret_cast<void*>(vtable);
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
}
} // namespace

CGlobalMap* CGlobalMap::Construct(CInterface3D* ui) {
    ConstructContainerAt(this, nullptr, ui);
    allodTitle_ = EmptyCString();
    EmptyArray(&zones_, 0x00740BFC);
    EmptyArray(&links_, 0x00740BF0);
    EmptyArray(&quests_, 0x00740BE4);
    path_.Start() = nullptr;
    path_.Finish() = nullptr;
    // The empty compare / allocator objects are passed by address: the original passes the
    // argument slot (holding `ui`) and a zeroed local; the tree keeps the slot's first byte.
    CInterface3D* slot = ui;
    std::uint32_t zero = 0;
    ConstructPair(&path_.EndOfStorage(), nullptr, &slot, &zero);
    target_ = EmptyCString();
    ConstructTree(&arrivals_, nullptr, &slot);
    arrivals_.Count() = 0;
    arrivals_.Compare() = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(slot));
    EmptyTree(&arrivals_);
    turn_.w = 1.0f;   // the identity turn
    turn_.x = 0.0f;
    turn_.y = 0.0f;
    turn_.z = 0.0f;
    shownName_ = EmptyCString();
    vtable_ = reinterpret_cast<void**>(0x00740B3C);
    inputVtable_ = reinterpret_cast<void**>(0x00740B08);
    return this;
}
EI_REPLACE_METHOD(0x005F67A0, CGlobalMap::Construct);

// --- 005F6920: the destructor body ---------------------------------------------------------------

namespace {
// The tree at +0x1E0 (thiscall): 005FB7D0 erases a subtree (RET 4), 005F9980 the destructor.
__declspec(naked) void __fastcall TreeErase(void*, void*, void*) EI_ORIGINAL(0x005FB7D0)
__declspec(naked) void __fastcall TreeDestruct(void*) EI_ORIGINAL(0x005F9980)
// 005FA7E0 (__fastcall: block, count): frees a vector block of 8-byte entries.
__declspec(naked) void __fastcall FreeBlock8(void*, int) EI_ORIGINAL(0x005FA7E0)
// The array entries' destructors (thiscall): quests / links (0x18 bytes), zones (0x10 bytes).
__declspec(naked) void __fastcall DestroyQuest(void*) EI_ORIGINAL(0x005FBDD0)
__declspec(naked) void __fastcall DestroyLink(void*) EI_ORIGINAL(0x005FBD80)
__declspec(naked) void __fastcall DestroyZone(void*) EI_ORIGINAL(0x005FBD30)
// 00600BA0: the container base's destructor body.

// A CArray's destructor: its vtable back, each entry destroyed, the storage freed.
template <typename Array>
void DestroyArray(Array* array, std::uintptr_t vtable, void(__fastcall* destroy)(void*)) {
    array->vtable = reinterpret_cast<void*>(vtable);
    if (!array->data) return;
    int size = array->size;
    for (int i = 0; i < size; ++i) destroy(&array->data[i]);
    gamecrt::operator_delete(array->data);
}
} // namespace

void __fastcall GlobalMapDestroy(CGlobalMap* self) {
    CStringDestroy(&self->shownName_);
    stlp::map<int, float>& tree = self->arrivals_;
    if (tree.size() != 0) {
        TreeErase(&tree, nullptr, tree.end()->parent);
        tree.ResetToEmpty();   // leftmost, root, rightmost, count
    }
    TreeDestruct(&tree);
    CStringDestroy(&self->target_);
    FreeBlock8(self->path_.Start(), static_cast<int>(self->path_.EndOfStorage() - self->path_.Start()));
    DestroyArray(&self->quests_, 0x00740BE4, DestroyQuest);
    DestroyArray(&self->links_, 0x00740BF0, DestroyLink);
    DestroyArray(&self->zones_, 0x00740BFC, DestroyZone);
    CStringDestroy(&self->allodTitle_);
    DestroyContainerBody(self);
}
EI_REPLACE(0x005F6920, GlobalMapDestroy);

// --- 00631CC0: the zone array's SetAtGrow --------------------------------------------------------

namespace {
mfc::CString& AsString(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }
const mfc::CString& AsString(char* const& field) { return *reinterpret_cast<const mfc::CString*>(&field); }
} // namespace

void GlobalMapZoneElements::Construct(GlobalMapZone* zones, int count) {
    std::memset(static_cast<void*>(zones), 0, static_cast<std::size_t>(count) * sizeof(GlobalMapZone));
    for (int i = 0; i < count; ++i) {
        new (&zones[i].name) mfc::CString();
        new (&zones[i].title) mfc::CString();
    }
}

void GlobalMapZoneElements::Destruct(GlobalMapZone* zones, int count) {
    for (int i = 0; i < count; ++i) {
        AsString(zones[i].title).~CString();
        AsString(zones[i].name).~CString();
    }
}

void __fastcall GlobalMapZonesSetAtGrow(GlobalMapZoneArray* zones, void*, int index, const GlobalMapZone* zone) {
    if (index >= zones->size) zones->SetSize(index + 1);
    GlobalMapZone& slot = zones->data[index];
    slot.state = zone->state;
    slot.link = zone->link;
    AsString(slot.name) = AsString(zone->name);
    AsString(slot.title) = AsString(zone->title);
}
EI_REPLACE(0x00631CC0, GlobalMapZonesSetAtGrow);
