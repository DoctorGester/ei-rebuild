// The objectives window (CObjectivesWindow, ui.h): its Create (0064BE80), the zone page
// (0064CC10) and the selected quest's description and places (0064DC60). TAB in a zone opens it
// for the current zone; opened with `listZones_` it lists the zones of the zone's allod in which
// the player has quests and shows one of them. See
// docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "stl_sorts.h"
#include "world_glue.h"
#include "text_reader.h"
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "maths.h"
#include "object_tables.h"
#include "quest_records.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/stlp_hash.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_engine.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <new>

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

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}


// --- Game data ----------------------------------------------------------------------------------

// A zone of the map database: its picture (picture2, the texture shown here), its size in map
// cells and its exits (the deploy rectangle x1, y1, x2, y2 and the target zone, "none" when it
// leads nowhere).
MapRecord* FindZone(const char* name) { return reinterpret_cast<CMapDatabase*>(0x0079BF00)->FindMap(name); }

CTextManager* Texts() { return *reinterpret_cast<CTextManager**>(0x007AF788); }
struct UiTexts {
    // 004DEFC0 (not rewritten). Finds text `key`; non-zero when found (the cursor at its start).
    int Find(const char* key);
};
int UiTexts::Find(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
int FindText(const char* key) { return reinterpret_cast<UiTexts*>(Texts())->Find(key); }

// The quest the window last selected (a global CString).
mfc::CString& LastQuest() { return *reinterpret_cast<mfc::CString*>(0x007C8178); }

// The local player's variables, copied by name prefix into an STLport hash_map<CString, float>.

// 00609C10 (not rewritten): hash_map constructor with at least `buckets` buckets (the three
// pointers are the hasher, key-equal and allocator objects, one byte each).
__declspec(naked) void __fastcall ConstructTable(NamedFloatTable*, void*, int /*buckets*/, const void*, const void*,
                                                 const void*) EI_ORIGINAL(0x00609C10)
// 004DBE40 (not rewritten): splits `text` at `separator` into `out`.

// A local hash_map<CString, float> filled with the player's variables starting with `prefix`.
class VariableTable {
public:
    VariableTable() {
        char functors[3] = {};
        ConstructTable(&table_, nullptr, 100, &functors[0], &functors[1], &functors[2]);
    }
    void Fill(const char* prefix) { GameLocalPlayerVariables(prefix, &table_); }
    ~VariableTable() { Destroy(); }

    // Iteration in STLport's order: buckets in order, each chain from its head.
    NamedFloatTable::Node* First() const {
        std::size_t n = BucketCount();
        for (std::size_t b = 0; b < n; ++b)
            if (table_.buckets[b]) return table_.buckets[b];
        return nullptr;
    }
    NamedFloatTable::Node* Next(const NamedFloatTable::Node* node) const {
        if (node->next) return node->next;
        std::size_t n = BucketCount();
        for (std::size_t b = stlp::HashText(node->key) % n + 1; b < n; ++b)
            if (table_.buckets[b]) return table_.buckets[b];
        return nullptr;
    }

private:
    std::size_t BucketCount() const { return static_cast<std::size_t>(table_.finish - table_.buckets); }

    // What the original's inlined destructor (and clear) do: every node's key released and the
    // node returned to the 16-byte free list, bucket by bucket, then the bucket storage.
    void Destroy() {
        for (std::size_t b = 0; b < BucketCount(); ++b) {
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

    NamedFloatTable table_;
};

using ZoneNames = stlp::vector<mfc::CString>;
int SizeOf(const ZoneNames& v) { return static_cast<int>(v.end() - v.begin()); }
// 0051F790 (not rewritten): vector<CString>::_M_insert_overflow(pos, x, n).
__declspec(naked) void __fastcall InsertOverflow(ZoneNames*, void*, mfc::CString* /*pos*/, const mfc::CString&,
                                                 int /*n*/) EI_ORIGINAL(0x0051F790)
// push_back as the original inlines it (the instantiation's own overflow).
void PushBack(ZoneNames* v, const mfc::CString& x) {
    if (v->Finish() != v->EndOfStorage()) {
        new (v->Finish()) mfc::CString(x);
        ++v->Finish();
    } else {
        InsertOverflow(v, nullptr, v->Finish(), x, 1);
    }
}

// std::sort as the original inlines it (the comparator is an empty object).
void SortStrings(mfc::CString* first, mfc::CString* last) {
    if (first == last) return;
    int depth = 0;
    for (int n = static_cast<int>(last - first); n != 1; n >>= 1) ++depth;
    stl_sorts::QuestIntroSort(first, last, nullptr, depth * 2, 0);
    if (last - first > 16) {
        stl_sorts::QuestInsertionSort(first, first + 16, 0);
        stl_sorts::QuestUnguardedInsertionSort(first + 16, last, nullptr, 0);
    } else {
        stl_sorts::QuestInsertionSort(first, last, 0);
    }
}

// The window's exit list: CArray<ObjectiveExit> at +0x188 (SetAtGrow 0064F160).

// The global map's exit distances (STLport map<int, float>).
using DistanceMap = stlp::map<int, float>;
using DistanceNode = DistanceMap::Node;

// A surface's text (005FDE20, Rebirth-patched).
int SurfaceText(CUiSurface* surface, const char* text, RECT* rect, int font, std::uint32_t colour, int format, int flag) {
    return SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, flag);
}

// 0065CC00 (not rewritten): the scrolling text window (a scroll bar subclass) over `rect`.
CUiWindow* __fastcall ConstructTextLog(void* memory, void*, CInterface3D* ui, const void* font, RECT rect, float lineHeight) {
    return static_cast<CUiScrollText*>(memory)->Construct(ui, static_cast<const char*>(font), rect, lineHeight);
}

// Sprite fields the builder sets directly.
void SetSpriteTexture(CUiMesh* sprite, CEngineObject* texture) { sprite->texture_ = texture; }
void SetSpriteColour(CUiMesh* sprite, std::uint32_t argb) { sprite->flatColour_ = argb; }
void SetSpritePlain(CUiMesh* sprite) { sprite->flags_ |= 0x100; }
void Turn(CUiMesh* sprite, float angle) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    std::memcpy(&sprite->localRotation_, &q, sizeof(q));
}

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    return region;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    if (!memory) return nullptr;
    return static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, 5, 1);
}

// Direct3D render states (Set fed to the renderer's mirror).
void SetRenderState(UiD3DDevice* device, int state, std::uint32_t value) {
    device->SetRenderState(static_cast<std::uint32_t>(state), value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
void GetRenderState(UiD3DDevice* device, int state, std::uint32_t* value) {
    device->GetRenderState(static_cast<std::uint32_t>(state), value);
}

// Layout constants (zone picture centre and size on the right half of the window).
constexpr float kPictureCentreX = 580.0f;   // 00742B38
constexpr float kPictureCentreY = 190.0f;   // 00742B3C
constexpr float kPictureHalf = 150.0f;      // 0073D618
constexpr float kTexels = 256.0f;           // 00742B40
constexpr float kPictureSpan = 300.0f;      // 0073FA98
constexpr float kMarkerHeight = 20.0f;      // 0073CCF8
constexpr float kMarkerHalfWidth = 10.0f;   // 0073BAAC
constexpr float kExitHalf = 15.0f;          // 0073C518
constexpr std::uint32_t kFarAway = 0x5A0E1BCA;   // 1e16
float ExitTurn() { return *reinterpret_cast<const float*>(0x007C8170); }   // a global angle
constexpr float kExitTurnDown = 3.1415927f; // 00742B24

} // namespace

// --- 0064CC10: the zone page ----------------------------------------------------------------------

void CObjectivesWindow::ShowZone(const char* zone) {
    *reinterpret_cast<mfc::CString*>(&shownZone_) = zone;
    // Drop what the previous zone added: its picture texture, exit markers and exit regions.
    while (objects_.size > 2) RemoveObjectAt(2);
    while (sceneObjects_.size > 0x15) RemoveSceneObjectAt(0x15);
    while (regions_.size > 9) RemoveRegionAt(9);

    MapRecord* map = FindZone(shownZone_);
    if (!map) {
        interface_->SetPendingValue(0);
        return;
    }

    // The zone picture (scene object 3), fitted into a 300x300 square around (580, 190).
    LoadTexture(map->picture2, 3, 0);
    float width = static_cast<float>(static_cast<double>(map->sizeX));
    float height = static_cast<float>(static_cast<double>(map->sizeY));
    float larger = width < height || x87::IsNaN(width) || x87::IsNaN(height) ? height : width;
    double widthShare = D(width) / D(larger);
    float heightShare = static_cast<float>(D(height) / D(larger));
    float halfHeight = static_cast<float>(D(heightShare) * kPictureHalf);
    float halfWidth = static_cast<float>(widthShare * kPictureHalf);
    int u1 = static_cast<int>(gamecrt::Ftol(widthShare * kTexels));
    int v0 = static_cast<int>(gamecrt::Ftol((1.0f - D(heightShare)) * kTexels));
    CUiMesh* picture = reinterpret_cast<CUiMesh*>(sceneObjects_.data[3]);
    picture->SetRectangle(10.0f, 100.0f, static_cast<float>(kPictureCentreX - D(halfWidth)), static_cast<float>(kPictureCentreY - D(halfHeight)),
                          static_cast<float>(D(halfWidth) + kPictureCentreX), static_cast<float>(D(halfHeight) + kPictureCentreY), 0, v0, u1,
                          0x100);
    SetSpriteTexture(picture, objects_.data[2]);

    // The exits that lead somewhere: a marker (triangle pointing out of the picture) and a region.
    float nearest = Bits(kFarAway);
    nearestExit_ = -1;
    bool anyKnown = false;
    mfc::CArray<ObjectiveExit>* exitArray = &exits_;
    exitArray->SetSize(0, -1);
    for (int i = 0; i < map->exits.size; ++i) {
        const MapExit& source = map->exits.data[i];
        if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(source.target),
                            reinterpret_cast<const unsigned char*>(game::Text(0x007846F0))) == 0)   // "none"
            continue;
        int index = exits_.size;
        ObjectiveExit blank = {};
        exitArray->SetAtGrow(index, blank);
        ObjectiveExit* exit = &exits_[index];
        exit->exit_ = i;
        exit->x_ = static_cast<float>((D(source.deploy[2]) + D(source.deploy[0])) * 0.5f);
        exit->y_ = static_cast<float>((D(source.deploy[3]) + D(source.deploy[1])) * 0.5f);
        exit->distance_ = 0.0f;
        exit->flag_ = false;
        if (exitDistances_) {
            // The distances the global map computed (map<int, float> exit -> distance).
            DistanceNode* header = exitDistances_->end();
            for (DistanceNode* node = header->left; node != exitDistances_->end();) {
                if (node->first == i) {
                    anyKnown = true;
                    std::memcpy(&exit->distance_, &node->second, 4);
                    exit->flag_ = true;
                    if (!(node->second >= nearest)) {
                        nearestExit_ = index;
                        std::memcpy(&nearest, &node->second, 4);
                    }
                    break;
                }
                node = DistanceMap::Next(node);
            }
        } else {
            mfc::CString name;
            name.Format(game::Text(0x00794148), source.target);   // "z.%s"
            float value = LocalPlayerVariable(name);
            if (value < 1.0f || value > 1.0f) exit->flag_ = true;   // not equal, NaN excluded
        }

        // Position on the picture (world cells are 32 units).
        float worldWidth = static_cast<float>(static_cast<double>(map->sizeX << 5));
        float worldHeight = static_cast<float>(static_cast<double>(map->sizeY << 5));
        float span = worldWidth < worldHeight || x87::IsNaN(worldWidth) || x87::IsNaN(worldHeight) ? worldHeight
                                                                                                   : worldWidth;
        exit = &exits_[index];
        double dx = D(exit->x_) - D(worldWidth) * 0.5f;
        float dy = static_cast<float>(D(exit->y_) - D(worldHeight) * 0.5f);
        float sideX = static_cast<float>(dx / D(worldWidth));
        float sideY = static_cast<float>(D(dy) / D(worldHeight));
        double mapX = dx * kPictureSpan / D(span) + kPictureCentreX;
        float x = static_cast<float>(mapX);
        float y = static_cast<float>(kPictureCentreY - D(dy) * kPictureSpan / D(span));
        exit->mapX_ = static_cast<float>(mapX);
        exit->mapY_ = y;

        CUiMesh* marker = AddSprite(0);
        float base = static_cast<float>(D(y) + kMarkerHeight);
        marker->SetTriangle(5.0f, 100.0f, x, y, static_cast<float>(D(x) - kMarkerHalfWidth), base, static_cast<float>(D(x) + kMarkerHalfWidth),
                            base, 240.0f, 226.0f, 227.0f, 254.0f, 253.0f, 254.0f);
        markerScale_ = marker->GetScaleX();
        RECT rect;
        rect.left = static_cast<int>(gamecrt::Ftol(D(x) - kExitHalf));
        rect.top = static_cast<int>(gamecrt::Ftol(D(y) - kExitHalf));
        rect.right = static_cast<int>(gamecrt::Ftol(D(x) + kExitHalf));
        rect.bottom = static_cast<int>(gamecrt::Ftol(D(y) + kExitHalf));
        CUiRegion* region = AddRegion();
        region->SetTip(0x138EE, 1, 0);
        // The marker points away from the picture's centre: sideways or up/down by the larger
        // offset, and the region moves 10 pixels the same way.
        if (AbsBits(sideY) >= AbsBits(sideX)) {
            if (sideY > 0.0f) {
                Turn(marker, kExitTurnDown);
                gameapi::OffsetRect(&rect, 0, -10);
            } else {
                gameapi::OffsetRect(&rect, 0, 10);
            }
        } else if (sideX > 0.0f) {
            Turn(marker, -ExitTurn());
            gameapi::OffsetRect(&rect, 10, 0);
        } else {
            Turn(marker, ExitTurn());
            gameapi::OffsetRect(&rect, -10, 0);
        }
        region->CallSetRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    SelectExit(nearestExit_);
    if (twoButtons_ != 0 && !anyKnown) {
        reinterpret_cast<CUiMesh*>(sceneObjects_.data[0])->visible_ = 0;
        regions_.data[0]->SetTip(0, 1, 0);
    }

    // The quests: the player's variables "q.<zone>.<quest>" that are not 0, sorted by name.
    mfc::CStringArray* quests = &quests_;
    quests->SetSize(0, -1);
    {
        mfc::CString prefix;
        CStringArray parts;
        alignas(VariableTable) unsigned char tableStorage[sizeof(VariableTable)];
        auto* table = new (tableStorage) VariableTable();
        prefix.Format(game::Text(0x00794140), shownZone_);   // "q.%s"
        table->Fill(prefix);
        ZoneNames found;
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            if (node->value == 0.0f || x87::IsNaN(node->value)) continue;
            text::Split(node->key, &parts, '.');
            if (parts.GetSize() == 3) PushBack(&found, parts[2]);
        }
        shownQuest_ = -1;
        selectedQuest_ = 0;
        if (SizeOf(found) == 0) {
            selectedQuest_ = -1;
            LastQuest().Empty();
        } else {
            SortStrings(found.begin(), found.end());
            for (mfc::CString* s = found.begin(); s != found.end(); ++s) {
                quests->SetAtGrow(quests->GetSize(), *s);
                const char* current = listZones_ == 0 ? static_cast<const char*>(LastQuest())
                                                      : static_cast<const char*>(interface_->savedText_);
                if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(current),
                                    reinterpret_cast<const unsigned char*>(static_cast<const char*>(*s))) == 0)
                    selectedQuest_ = quests->GetSize() - 1;
            }
            LastQuest() = (*quests)[selectedQuest_];
        }
        int rows = quests->GetSize();
        bar_->SetMaximum(rows > 5 ? rows - 5 : 0);
        bar_->CallSetPosition(0);
        firstRow_ = 0;
        RefreshQuests();

        // The zone's title and description ("zone <name>") on surface 1.
        {
            mfc::CString text;    // (declared first: the original releases the title first)
            mfc::CString title;
            int foundText;
            {
                mfc::CString zoneKey(game::Text(0x007941F8));   // "zone "
                mfc::CString key = zoneKey + *reinterpret_cast<const mfc::CString*>(&shownZone_);
                foundText = FindText(key);
            }
            if (foundText) {
                Texts()->ReadLine(&title, 1);
                Texts()->ReadToHash(&text, 1);
                CUiSurface* surface = surfaces_.data[1];
                UiSurfaceGetDC(surface);
                SurfaceClearOriginal(surface);
                RECT titleRect = {0, 5, 340, 25};
                SurfaceText(surface, title, &titleRect, 1, 0x31B3FF, 1, 1);
                RECT textRect = {10, 25, 330, 140};
                SurfaceText(surface, text, &textRect, 1, 0xA7D7E4, 0x11, 1);
                UiSurfaceReleaseDC(surface);
            }
        }

        for (mfc::CString* s = found.begin(); s != found.end(); ++s) s->~CString();
        found.FreeStorage();
        table->~VariableTable();
    }
}

// --- 0064DC60: the selected quest ------------------------------------------------------------------

namespace {

// The text reader's line steps (text_reader.cpp; called through their addresses).
// The quest log (the window's first child) [thiscall]: clear [0065D450], set the text
// [0065CFE0].
// The quest database's index (007C87B4, map<string, record*>): operator[] [0064EE90], creating an
// empty slot for a name it lacks.
QuestDatabase::QuestIndex& QuestIndex() { return *reinterpret_cast<QuestDatabase::QuestIndex*>(0x007C87B4); }

// The quest's record (its experience and money rewards), from the quest database.
QuestRecord* FindQuestRecord(const char* name) {
    stlp::string key(name);
    return QuestIndex().Subscript(key);
}

bool SameText(const char* a, const char* b) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}

} // namespace

void CObjectivesWindow::ShowQuest() {
    CTextManager* texts = Texts();
    mfc::CString line;
    mfc::CString part;
    mfc::CString text;
    CStringArray fields;
    mfc::CString variable;
    if (shownQuest_ == selectedQuest_) return;
    shownQuest_ = selectedQuest_;
    static_cast<CUiScrollText*>(children_.data[0])->Clear();
    mfc::CStringArray* quests = &quests_;
    if (selectedQuest_ < 0 || selectedQuest_ >= quests->GetSize()) return;
    LastQuest() = (*quests)[selectedQuest_];

    mfc::CString completed;
    mfc::CString failed;
    mfc::CString costExp;
    mfc::CString costMoney;
    texts->GetString(game::Text(0x0079472C), &completed);   // "obj_completed"
    texts->GetString(game::Text(0x00794720), &failed);      // "obj_failed"
    texts->GetString(game::Text(0x00794710), &costExp);     // "obj_cost_exp"
    texts->GetString(game::Text(0x00794700), &costMoney);   // "obj_cost_money"
    bool found;
    {
        mfc::CString questWord(game::Text(0x00790790));     // "quest "
        mfc::CString key = questWord + (*quests)[selectedQuest_];
        found = FindText(key) != 0;
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    TextNextLine(texts);
    texts->ReadToHash(&text, 1);
    {
        mfc::CString colour(game::Text(0x007946F8));
        text = colour + text;
    }
    text += game::Text(0x007946F4);
    if (QuestRecord* record = FindQuestRecord((*quests)[selectedQuest_])) {
        float experience = record->exp;
        text += game::Text(0x007946EC);
        if (experience > 0.0f) {   // 0073B858
            part.Format(game::Text(0x007946E4), static_cast<const char*>(costExp), static_cast<int>(gamecrt::Ftol(D(experience))));   // "%s %d\n"
            text += part;
        }
        float money = record->money;
        if (money > 0.0f) {
            part.Format(game::Text(0x007946E4), static_cast<const char*>(costMoney), static_cast<int>(gamecrt::Ftol(D(money))));
            text += part;
        }
    }
    while (TextFindLineOriginal(texts, nullptr, nullptr)) {
        texts->Split(&fields, 1);
        if (fields.GetSize() < 2) continue;
        if (SameText(fields[0], game::Text(0x007946DC))) break;      // "#quest"
        if (!SameText(fields[0], game::Text(0x007946D4))) continue;  // "#subobj"
        variable.Format(game::Text(0x007946C8), shownZone_, static_cast<const char*>((*quests)[selectedQuest_]),
                        static_cast<const char*>(fields[1]));   // "q.%s.%s.%s"
        int state = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable(variable))));
        if (state == 0) continue;
        texts->ReadLine(&line, 1);
        text += game::Text(0x0079349C);
        text += line;
        text += game::Text(0x0078DA40);   // " "
        if (state == 2) {
            text += game::Text(0x007946B8);
            text += completed;
        } else if (state == 3) {
            text += game::Text(0x007946C0);
            text += failed;
        }
        text += game::Text(0x007946AC);
        texts->ReadToHash(&line, 1);
        text += line;
        text += game::Text(0x007946A4);
    }
    TextClose(texts);
    static_cast<CUiScrollText*>(children_.data[0])->SetText(text);

    // The quest's places on the zone picture: a red marker and a region each, after the exits'.
    MapRecord* map = FindZone(shownZone_);
    if (!map) return;
    LoadQuestPlaces((*quests)[selectedQuest_]);
    while (sceneObjects_.size > exits_.size + 0x15) RemoveSceneObjectAt(exits_.size + 0x15);
    while (regions_.size > exits_.size + 9) RemoveRegionAt(exits_.size + 9);
    const float red[3] = {1.0f, 0.0f, 0.0f};
    for (unsigned i = 0; i < static_cast<unsigned>(questPlaces_.end() - questPlaces_.begin()); ++i) {
        float worldWidth = static_cast<float>(static_cast<double>(map->sizeX << 5));
        float worldHeight = static_cast<float>(static_cast<double>(map->sizeY << 5));
        float span = worldWidth < worldHeight || x87::IsNaN(worldWidth) || x87::IsNaN(worldHeight) ? worldHeight
                                                                                                   : worldWidth;
        const UiPointF& place = questPlaces_[i];
        float x = static_cast<float>((D(place.x) - D(worldWidth) * 0.5f) * kPictureSpan / D(span) + kPictureCentreX);
        float y = static_cast<float>(kPictureCentreY - (D(place.y) - D(worldHeight) * 0.5f) * kPictureSpan / D(span));
        CUiMesh* marker = AddSprite(0);
        float bottom = static_cast<float>(D(y) + kMarkerHalfWidth);
        float right = static_cast<float>(D(x) + kMarkerHalfWidth);
        float top = static_cast<float>(D(y) - kMarkerHalfWidth);
        float left = static_cast<float>(D(x) - kMarkerHalfWidth);
        marker->SetRectangle(5.0f, 100.0f, left, top, right, bottom, 0, 0, 0x80, 0x80);
        SetSpriteTexture(marker, objects_.data[1]);
        std::memcpy(marker->tint_, red, sizeof(red));
        CUiRegion* region = AddRegion();
        int bottomPixel = static_cast<int>(gamecrt::Ftol(D(bottom)));
        int rightPixel = static_cast<int>(gamecrt::Ftol(D(right)));
        int topPixel = static_cast<int>(gamecrt::Ftol(D(top)));
        int leftPixel = static_cast<int>(gamecrt::Ftol(D(left)));
        region->CallSetRect(leftPixel, topPixel, rightPixel, bottomPixel);
        region->SetTip(0x138F0, 1, 0);
    }
}

// --- 0064BE80: Create ------------------------------------------------------------------------------

void CObjectivesWindow::Build() {
    UiD3DDevice* device = interface_->renderer_->device_;
    GetRenderState(device, kRenderStateAlphaRef, &savedAlphaRef_);
    SetRenderState(device, kRenderStateAlphaRef, 1);

    mfc::CString text;
    mfc::CString key;
    mfc::CStringArray* objectiveTexts = &objectiveTexts_;
    for (int k = 0; k < 3; ++k) {
        key.Format(game::Text(0x00794670), k);   // "obj_%d"
        Texts()->GetString(key, &text);
        objectiveTexts->SetAtGrow(objectiveTexts->GetSize(), text);
    }

    // The quest log (left, below the list) and the list's scroll bar.
    void* memory = gamecrt::operator_new(0x1C8);
    AddChild(memory ? ConstructTextLog(memory, nullptr, interface_, game::EmptyText(),
                                       RECT{60, 190, 384, 510}, 10.0f)
                    : nullptr);
    memory = gamecrt::operator_new(0x18C);
    bar_ = memory ? static_cast<CUiScrollbar*>(memory)->Construct(interface_, RECT{60, 30, 384, 150}, 3.0f, 0, 1.0f)
                  : nullptr;
    AddChild(bar_);

    LoadTexture(game::Text(0x00793380), 3, 0);   // "saveload"
    LoadTexture(game::Text(0x00794668), 3, 0);   // "cross"
    AddSurface(NewSurface(interface_, 50, 20, 390, 160));
    AddSurface(NewSurface(interface_, 410, 380, 750, 520));
    AddSurface(NewSurface(interface_, 280, 540, 520, 580));

    // Scene objects 0-20 (the zone page adds its markers after them).
    if (twoButtons_)
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 163.0f, 536.0f, 237.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    else
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 363.0f, 536.0f, 437.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    CUiMesh* second = AddSprite(0);
    second->SetRectangle(3.0f, 100.0f, 563.0f, 536.0f, 637.0f, 584.0f, 0xA0, 2, 0xEA, 0x32);
    if (!twoButtons_) second->visible_ = 0;
    CUiMesh* highlight = AddSprite(0);
    highlight->SetRectangle(3.0f, 100.0f, 60.0f, 30.0f, 370.0f, 54.0f, 0, 0, 0x100, 0x100);
    SetSpriteTexture(highlight, nullptr);
    SetSpriteColour(highlight, 0xC8A06800);
    SetSpritePlain(highlight);
    highlight->visible_ = 0;
    AddSprite(0);   // object 3: the zone picture, set up by the zone page
    CUiMesh* bottomShade = AddSprite(0);
    bottomShade->SetRectangle(15.0f, 100.0f, 285.0f, 541.0f, 515.0f, 579.0f, 0, 0, 0x100, 0x100);
    SetSpriteTexture(bottomShade, nullptr);
    SetSpritePlain(bottomShade);
    SetSpriteColour(bottomShade, 0xA0000000);
    AddSprite(0)->SetFrame(3.0f, 100.0f, 280.0f, 536.0f, 520.0f, 584.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    const float shades[4][4] = {{50.0f, 20.0f, 390.0f, 160.0f},
                                {50.0f, 180.0f, 390.0f, 520.0f},
                                {410.0f, 20.0f, 750.0f, 360.0f},
                                {410.0f, 380.0f, 750.0f, 520.0f}};
    for (const auto& s : shades) {
        CUiMesh* shade = AddSprite(0);
        shade->SetRectangle(15.0f, 100.0f, s[0], s[1], s[2], s[3], 0, 0, 0x100, 0x100);
        SetSpriteTexture(shade, nullptr);
        SetSpritePlain(shade);
        SetSpriteColour(shade, 0xA0000000);
    }
    const float frames[4][4] = {{45.0f, 15.0f, 395.0f, 165.0f},
                                {45.0f, 175.0f, 395.0f, 525.0f},
                                {405.0f, 15.0f, 755.0f, 365.0f},
                                {405.0f, 375.0f, 755.0f, 525.0f}};
    for (const auto& f : frames)
        AddSprite(0)->SetFrame(3.0f, 100.0f, f[0], f[1], f[2], f[3], 5.0f, 2, 0xFE, 0xB4, 0xF9);
    // The two arrows that switch zones (objects 14 and 15), hidden until there are several.
    CUiMesh* left = AddSprite(0);
    left->SetRectangle(3.0f, 100.0f, 414.0f, 384.0f, 443.0f, 404.0f, 2, 0x72, 0x1F, 0x86);
    left->visible_ = 0;
    CUiMesh* right = AddSprite(0);
    right->SetRectangle(3.0f, 100.0f, 717.0f, 384.0f, 746.0f, 404.0f, 0x21, 0x72, 0x3E, 0x86);
    right->visible_ = 0;
    // The five row markers (objects 16-20), textured with the cross.
    for (int top = 31; top < 151; top += 24) {
        CUiMesh* mark = AddSprite(0);
        mark->SetRectangle(2.8f, 100.0f, 62.0f, static_cast<float>(top), 79.0f, static_cast<float>(top + 22), 4,
                           0xA4, 0x48, 0xFC);
        mark->visible_ = 0;
        SetSpriteTexture(mark, objects_.data[1]);
    }

    // Regions 0-3: the buttons and the two arrows; 4-8: the list rows.
    if (twoButtons_)
        AddRect(this, 163, 536, 237, 584)->SetTip(0x138E4, 1, 0);
    else
        AddRect(this, 363, 536, 437, 584)->SetTip(0x138E6, 1, 0);
    AddRect(this, 563, 536, 637, 584)->SetTip(twoButtons_ ? 0x138E5 : 0, 1, 0);
    AddRect(this, 414, 384, 443, 404);
    AddRect(this, 717, 384, 746, 404);
    for (int top = 30; top < 150; top += 24) AddRect(this, 60, top, 360, top + 24)->SetTip(0x138EF, 1, 0);
    blinkTime_ = 0.0f;

    if (listZones_ == 0) {
        ShowZone(zoneName_);
    } else {
        MapRecord* allodZone = FindZone(zoneName_);
        if (!allodZone) {
            interface_->SetPendingValue(0);
            return;
        }
        // The zones of this allod in which the player holds quests ("q.<zone>.<quest>" = 1), and
        // the zone the interface's current quest (+0xAC) belongs to.
        mfc::CString current;
        CStringArray parts;
        alignas(VariableTable) unsigned char tableStorage[sizeof(VariableTable)];
        auto* table = new (tableStorage) VariableTable();
        table->Fill(game::Text(0x00783D7C));   // "q."
        ZoneNames* zones = &zones_;
        const char* interfaceQuest = interface_->savedText_;
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            text::Split(node->key, &parts, '.');
            if (parts.GetSize() != 3) continue;
            if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(interfaceQuest),
                                reinterpret_cast<const unsigned char*>(static_cast<const char*>(parts[2]))) == 0)
                current = parts[1];
            std::uint32_t one;
            std::memcpy(&one, &node->value, 4);
            if (one != 0x3F800000) continue;
            MapRecord* zone = FindZone(parts[1]);
            if (!zone) continue;
            if (!IsMultiplayerClient() &&
                libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(zone->allod),
                                reinterpret_cast<const unsigned char*>(allodZone->allod)) != 0)
                continue;
            bool listed = false;
            for (mfc::CString* z = zones->begin(); z != zones->end(); ++z)
                if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(zone->name),
                                    reinterpret_cast<const unsigned char*>(static_cast<const char*>(*z))) == 0) {
                    listed = true;
                    break;
                }
            if (!listed) PushBack(zones, *reinterpret_cast<const mfc::CString*>(&zone->name));
        }
        if (SizeOf(*zones) == 0 && reinterpret_cast<const int*>(static_cast<const char*>(current))[-2] != 0)
            PushBack(zones, current);
        if (SizeOf(*zones) == 0) {
            interface_->SetPendingValue(0);
            table->~VariableTable();
            return;
        }
        zoneIndex_ = 0;
        for (int i = 0; i < SizeOf(*zones); ++i)
            if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(current)),
                                reinterpret_cast<const unsigned char*>(static_cast<const char*>(zones->begin()[i]))) ==
                0) {
                zoneIndex_ = i;
                break;
            }
        if (SizeOf(*zones) > 1) {
            reinterpret_cast<CUiMesh*>(sceneObjects_.data[14])->visible_ = 1;
            reinterpret_cast<CUiMesh*>(sceneObjects_.data[15])->visible_ = 1;
            regions_.data[2]->SetTip(0x13948, 1, 0);
            regions_.data[3]->SetTip(0x13949, 1, 0);
        }
        ShowZone(zones->begin()[zoneIndex_]);
        table->~VariableTable();
    }
    interface_->ShowHelp(0);
}
void CObjectivesWindow::DrawQuestList() {
    CTextManager* texts = Texts();
    mfc::CString title;
    mfc::CString variable;
    auto mesh = [this](int index) { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[index]); };
    mesh(2)->visible_ = 0;
    auto* surface = static_cast<CUiSurface*>(surfaces_.data[0]);
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    for (int i = 16; i < 21; ++i) mesh(i)->visible_ = 0;
    mfc::CStringArray* quests = &quests_;
    int row = firstRow_;
    int y = 13;
    for (int i = 16; i < 21 && row < quests->GetSize(); ++i, ++row, y += 24) {
        CUiMesh* mark = mesh(i);
        mark->visible_ = 1;
        bool found;
        {
            mfc::CString questWord(game::Text(0x00790790));     // "quest "
            mfc::CString key = questWord + (*quests)[row];
            found = FindText(key) != 0;
        }
        if (found) texts->ReadLine(&title, 1);
        else title = (*quests)[row];
        variable.Format(game::Text(0x0079469C), shownZone_, static_cast<const char*>((*quests)[row]));   // "q.%s.%s"
        int state = static_cast<int>(LocalPlayerVariable(variable));
        if (state == 2) mark->textureOffsetU_ = 0.265625f;
        else if (state == 3) mark->textureOffsetU_ = 0.0f;
        else mark->visible_ = 0;
        if (row == selectedQuest_) {
            SpritePlaceOriginal(mesh(2), nullptr, 215.0f, static_cast<float>(y + 29), 3.0f);
            mesh(2)->visible_ = 1;
        }
        RECT rect{36, y, 320, y + 21};
        SurfaceDrawText(surface, nullptr, title, &rect, 1, 0xA7D7E4, 0, 1);
    }
    UiSurfaceReleaseDC(surface);
}

EI_REPLACE_METHOD(0x0064BE80, CObjectivesWindow::Build);
EI_REPLACE_METHOD(0x0064D9B0, CObjectivesWindow::DrawQuestList);
EI_REPLACE_METHOD(0x0064CC10, CObjectivesWindow::ShowZone);
EI_REPLACE_METHOD(0x0064DC60, CObjectivesWindow::ShowQuest);

// --- 0064BE40 / 0064D990 (A-H25): what the window is opened for, and its help topic --------------------

namespace {
const char* const kGlobalMapTopic = reinterpret_cast<const char*>(std::uintptr_t(0x00794688));   // "quest_global_map"
const char* const kMissionTopic = reinterpret_cast<const char*>(std::uintptr_t(0x00794678));     // "quest_mission"
} // namespace

void CObjectivesWindow::Setup(const char* zone, void* exitDistances, int twoButtons, int listZones) {
    *reinterpret_cast<mfc::CString*>(&zoneName_) = zone;
    twoButtons_ = twoButtons;
    exitDistances_ = static_cast<stlp::map<int, float>*>(exitDistances);
    listZones_ = listZones;
}
EI_REPLACE_METHOD(0x0064BE40, CObjectivesWindow::Setup);

const char* CObjectivesWindow::HelpTopic() const { return twoButtons_ ? kGlobalMapTopic : kMissionTopic; }
EI_REPLACE_METHOD(0x0064D990, CObjectivesWindow::HelpTopic);
