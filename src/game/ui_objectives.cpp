// The objectives window (CObjectivesWindow, ui.h): its Create (0064BE80), the zone page
// (0064CC10) and the selected quest's description and places (0064DC60). TAB in a zone opens it
// for the current zone; opened with `listZones_` it lists the zones of the zone's allod in which
// the player has quests and shows one of them. See
// docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the
// operation order is kept and NaN compares stay as written.
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "maths.h"
#include "object_tables.h"
#include "world.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_engine.h"

#include <cstdint>
#include <cstring>
#include <new>

#pragma float_control(precise, on)

using console::CStringArray;

namespace {

inline double D(float f) { return f; }

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

float Bits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// The game's own strings are passed (callees may keep the pointer).
const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

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

// A zone exit as the map database keeps it (0x3C bytes; not the world's ZoneExit layout).
struct MapExitView {
    std::uint8_t unknown_00[4];
    float x1, y1, x2, y2;            // +0x04 rectangle
    std::uint8_t unknown_14[0x28 - 0x14];
    char* target;                    // +0x28 CString ("none": leads nowhere)
    std::uint8_t unknown_2C[0x3C - 0x2C];
};
static_assert(sizeof(MapExitView) == 0x3C);

// A zone of the map database (MapRecord, object_tables.h), with the fields used here.
struct ZoneView {
    char* name;                      // +0x00 CString
    char* allod;                     // +0x04 CString
    std::uint8_t unknown_08[0x14 - 0x08];
    char* picture;                   // +0x14 CString: the zone picture's texture
    std::uint8_t unknown_18[0x20 - 0x18];
    int width;                       // +0x20 in map cells
    int height;                      // +0x24
    std::uint8_t unknown_28[0x4C - 0x28];
    MapExitView* exits;              // +0x4C CArray data
    int exitCount;                   // +0x50
};
static_assert(offsetof(ZoneView, exits) == 0x4C);

ZoneView* FindZone(const char* name) {
    return reinterpret_cast<ZoneView*>(reinterpret_cast<CMapDatabase*>(0x0079BF00)->FindMap(name));
}

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
struct LocalPlayerAt {
    // 00578B70 (not rewritten; the object at 007AFEA5): copies the variables whose names start
    // with `prefix`.
    void CopyVariables(const char* prefix, NamedFloatTable* out);
};
__declspec(naked) void LocalPlayerAt::CopyVariables(const char*, NamedFloatTable*) EI_ORIGINAL(0x00578B70)
LocalPlayerAt* LocalPlayerVariables() { return reinterpret_cast<LocalPlayerAt*>(0x007AFEA5); }

// 00609C10 (not rewritten): hash_map constructor with at least `buckets` buckets (the three
// pointers are the hasher, key-equal and allocator objects, one byte each).
__declspec(naked) void __fastcall ConstructTable(NamedFloatTable*, void*, int /*buckets*/, const void*, const void*,
                                                 const void*) EI_ORIGINAL(0x00609C10)
// 00460BA0 (STLport __stl_hash_string, not rewritten).
__declspec(naked) std::uint32_t __fastcall HashName(const char*) EI_ORIGINAL(0x00460BA0)
// 004DBE40 (not rewritten): splits `text` at `separator` into `out`.

// A local hash_map<CString, float> filled with the player's variables starting with `prefix`.
class VariableTable {
public:
    VariableTable() {
        char functors[3] = {};
        ConstructTable(&table_, nullptr, 100, &functors[0], &functors[1], &functors[2]);
    }
    void Fill(const char* prefix) { LocalPlayerVariables()->CopyVariables(prefix, &table_); }
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
        for (std::size_t b = HashName(node->key) % n + 1; b < n; ++b)
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

// An STLport vector<CString> (three pointers).
struct StringVector {
    mfc::CString* begin;
    mfc::CString* end;
    mfc::CString* capacityEnd;
    int Size() const { return static_cast<int>(end - begin); }
};
// 0051F790 (not rewritten): vector<CString>::_M_insert_overflow(pos, x, n).
__declspec(naked) void __fastcall InsertOverflow(StringVector*, void*, mfc::CString* /*pos*/, const mfc::CString&,
                                                 int /*n*/) EI_ORIGINAL(0x0051F790)
void PushBack(StringVector* v, const mfc::CString& x) {
    if (v->end != v->capacityEnd) {
        new (v->end) mfc::CString(x);
        ++v->end;
    } else {
        InsertOverflow(v, nullptr, v->end, x, 1);
    }
}

// STLport sort<CString*> pieces (not rewritten): the introsort loop and the two insertion sorts.
__declspec(naked) void __fastcall IntroSortLoop(mfc::CString* /*first*/, mfc::CString* /*last*/, void*, int /*depth*/,
                                                int /*comp*/) EI_ORIGINAL(0x0064F310)
__declspec(naked) void __fastcall InsertionSort(mfc::CString*, mfc::CString*, int /*comp*/) EI_ORIGINAL(0x0064F9A0)
__declspec(naked) void __fastcall UnguardedInsertionSort(mfc::CString*, mfc::CString*, void*, int /*comp*/)
    EI_ORIGINAL(0x0064FBD0)

// std::sort as the original inlines it (the comparator is an empty object).
void SortStrings(mfc::CString* first, mfc::CString* last) {
    if (first == last) return;
    int depth = 0;
    for (int n = static_cast<int>(last - first); n != 1; n >>= 1) ++depth;
    IntroSortLoop(first, last, nullptr, depth * 2, 0);
    if (last - first > 16) {
        InsertionSort(first, first + 16, 0);
        UnguardedInsertionSort(first + 16, last, nullptr, 0);
    } else {
        InsertionSort(first, last, 0);
    }
}

// The window's exit list (CArray<ObjectiveExit> at +0x188), not rewritten.
struct ExitArrayAt {
    void SetSize(int size, int growBy);                    // 0064EFD0
    void SetAtGrow(int index, const ObjectiveExit& exit);  // 0064F160
};
__declspec(naked) void ExitArrayAt::SetSize(int, int) EI_ORIGINAL(0x0064EFD0)
__declspec(naked) void ExitArrayAt::SetAtGrow(int, const ObjectiveExit&) EI_ORIGINAL(0x0064F160)

// STLport map<int, float> (rb-tree) nodes: key +0x10, value +0x14; the header's left is begin.
struct DistanceNode {
    std::uint8_t unknown_00[8];
    DistanceNode* leftmost;          // +0x08 (the header's: begin())
    std::uint8_t unknown_0C[4];
    int exit;                        // +0x10
    float distance;                  // +0x14
};
// 0043DBE0 (not rewritten): rb-tree iterator increment (thiscall on the iterator).
void __fastcall TreeIncrement(DistanceNode** a0) { TreeIncrementOriginal((void*)a0); }

// Surfaces and sprites (not rewritten).
struct SurfaceDraw {
    void* Begin() { return SurfaceGetDC(this); }                                                                   // 005FDC00
    void Clear();                                                                   // 005FE2F0
    int Text(const char* text, RECT* rect, int a, std::uint32_t colour, int format, int b);   // 005FDE20 (Rebirth)
    void End() { SurfaceReleaseDC(this); }                                                                     // 005FDC60
};
__declspec(naked) void SurfaceDraw::Clear() EI_ORIGINAL(0x005FE2F0)
int SurfaceDraw::Text(const char* text, RECT* rect, int format, std::uint32_t colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

struct SpriteAt {
    // 005E99D0 (Rebirth-patched): a triangle sprite (corners (x0,y0), (x1,y1), (x2,y2) at `depth`,
    // texels of its three corners).
    void SetTriangle(float depth, float size, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                     float v0, float u1, float v1, float u2, float v2);
    // 005E70B0: the sprite's +0x23C, left in ST0.
    double Scale();
};
__declspec(naked) void SpriteAt::SetTriangle(float, float, float, float, float, float, float, float, float, float,
                                             float, float, float, float) EI_ORIGINAL(0x005E99D0)
__declspec(naked) double SpriteAt::Scale() EI_ORIGINAL(0x005E70B0)

struct ScrollbarAt {
    void SetRange(int maximum);      // 0065AFE0
};
__declspec(naked) void ScrollbarAt::SetRange(int) EI_ORIGINAL(0x0065AFE0)

// 0065CC00 (not rewritten): the scrolling text window (a scroll bar subclass) over `rect`.
__declspec(naked) CUiWindow* __fastcall ConstructTextLog(void* /*memory*/, void*, CInterface3D*, const void* /*font*/,
                                                         RECT, float /*lineHeight*/) EI_ORIGINAL(0x0065CC00)

// Sprite fields the builder sets directly.
void SetSpriteTexture(CUiMesh* sprite, void* texture) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0x280, &texture, 4);
}
void SetSpriteColour(CUiMesh* sprite, std::uint32_t argb) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0xD4, &argb, 4);
}
void SetSpritePlain(CUiMesh* sprite) {
    std::uint32_t flags;
    std::memcpy(&flags, reinterpret_cast<std::uint8_t*>(sprite) + 0x08, 4);
    flags |= 0x100;
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0x08, &flags, 4);
}
void Turn(CUiMesh* sprite, float angle) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0x34, &q, sizeof(q));
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

// Direct3D render states through the device's vtable (slot 20 Set, 21 Get).
void SetRenderState(UiD3DDevice* device, int state, std::uint32_t value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t);
    reinterpret_cast<Fn>(device->vtable_[20])(device, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
void GetRenderState(UiD3DDevice* device, int state, std::uint32_t* value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t*);
    reinterpret_cast<Fn>(device->vtable_[21])(device, state, value);
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

__declspec(naked) void CObjectivesWindow::SelectExit(int) EI_ORIGINAL(0x0064E990)
__declspec(naked) void CObjectivesWindow::RefreshQuests() EI_ORIGINAL(0x0064E8B0)

// --- 0064CC10: the zone page ----------------------------------------------------------------------

void CObjectivesWindow::ShowZone(const char* zone) {
    *reinterpret_cast<mfc::CString*>(&shownZone_) = zone;
    // Drop what the previous zone added: its picture texture, exit markers and exit regions.
    while (objects_.size_ > 2) RemoveObjectAt(2);
    while (sceneObjects_.size_ > 0x15) RemoveSceneObjectAt(0x15);
    while (regions_.size_ > 9) RemoveRegionAt(9);

    ZoneView* map = FindZone(shownZone_);
    if (!map) {
        interface_->SetPendingValue(0);
        return;
    }

    // The zone picture (scene object 3), fitted into a 300x300 square around (580, 190).
    LoadTexture(map->picture, 3, 0);
    float width = Store(static_cast<double>(map->width));
    float height = Store(static_cast<double>(map->height));
    float larger = width < height || x87::IsNaN(width) || x87::IsNaN(height) ? height : width;
    double widthShare = D(width) / D(larger);
    float heightShare = Store(D(height) / D(larger));
    float halfHeight = Store(D(heightShare) * kPictureHalf);
    float halfWidth = Store(widthShare * kPictureHalf);
    int u1 = GameFtol(widthShare * kTexels);
    int v0 = GameFtol((1.0f - D(heightShare)) * kTexels);
    CUiMesh* picture = reinterpret_cast<CUiMesh*>(sceneObjects_.data_[3]);
    picture->SetRectangle(10.0f, 100.0f, Store(kPictureCentreX - D(halfWidth)), Store(kPictureCentreY - D(halfHeight)),
                          Store(D(halfWidth) + kPictureCentreX), Store(D(halfHeight) + kPictureCentreY), 0, v0, u1,
                          0x100);
    SetSpriteTexture(picture, objects_.data_[2]);

    // The exits that lead somewhere: a marker (triangle pointing out of the picture) and a region.
    float nearest = Bits(kFarAway);
    nearestExit_ = -1;
    bool anyKnown = false;
    auto* exitArray = reinterpret_cast<ExitArrayAt*>(&exitsVtable_);
    exitArray->SetSize(0, -1);
    for (int i = 0; i < map->exitCount; ++i) {
        const MapExitView& source = map->exits[i];
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(source.target),
                             reinterpret_cast<const unsigned char*>(GameText(0x007846F0))) == 0)   // "none"
            continue;
        int index = exitCount_;
        ObjectiveExit blank = {};
        exitArray->SetAtGrow(index, blank);
        ObjectiveExit* exit = &exits_[index];
        exit->exit_ = i;
        exit->x_ = Store((D(source.x2) + D(source.x1)) * 0.5f);
        exit->y_ = Store((D(source.y2) + D(source.y1)) * 0.5f);
        exit->distance_ = 0.0f;
        exit->flag_ = false;
        if (exitDistances_) {
            // The distances the global map computed (map<int, float> exit -> distance).
            DistanceNode* header = *static_cast<DistanceNode**>(exitDistances_);
            for (DistanceNode* node = header->leftmost; node != *static_cast<DistanceNode**>(exitDistances_);) {
                if (node->exit == i) {
                    anyKnown = true;
                    std::memcpy(&exit->distance_, &node->distance, 4);
                    exit->flag_ = true;
                    if (!(node->distance >= nearest)) {
                        nearestExit_ = index;
                        std::memcpy(&nearest, &node->distance, 4);
                    }
                    break;
                }
                TreeIncrement(&node);
            }
        } else {
            mfc::CString name;
            name.Format(GameText(0x00794148), source.target);   // "z.%s"
            float value = LocalPlayerVariable(name);
            if (value < 1.0f || value > 1.0f) exit->flag_ = true;   // not equal, NaN excluded
        }

        // Position on the picture (world cells are 32 units).
        float worldWidth = Store(static_cast<double>(map->width << 5));
        float worldHeight = Store(static_cast<double>(map->height << 5));
        float span = worldWidth < worldHeight || x87::IsNaN(worldWidth) || x87::IsNaN(worldHeight) ? worldHeight
                                                                                                   : worldWidth;
        exit = &exits_[index];
        double dx = D(exit->x_) - D(worldWidth) * 0.5f;
        float dy = Store(D(exit->y_) - D(worldHeight) * 0.5f);
        float sideX = Store(dx / D(worldWidth));
        float sideY = Store(D(dy) / D(worldHeight));
        double mapX = dx * kPictureSpan / D(span) + kPictureCentreX;
        float x = Store(mapX);
        float y = Store(kPictureCentreY - D(dy) * kPictureSpan / D(span));
        exit->mapX_ = Store(mapX);
        exit->mapY_ = y;

        CUiMesh* marker = AddSprite(0);
        float base = Store(D(y) + kMarkerHeight);
        reinterpret_cast<SpriteAt*>(marker)->SetTriangle(5.0f, 100.0f, x, y, Store(D(x) - kMarkerHalfWidth), base,
                                                         Store(D(x) + kMarkerHalfWidth), base, 240.0f, 226.0f, 227.0f,
                                                         254.0f, 253.0f, 254.0f);
        markerScale_ = Store(reinterpret_cast<SpriteAt*>(marker)->Scale());
        RECT rect;
        rect.left = GameFtol(D(x) - kExitHalf);
        rect.top = GameFtol(D(y) - kExitHalf);
        rect.right = GameFtol(D(x) + kExitHalf);
        rect.bottom = GameFtol(D(y) + kExitHalf);
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
        reinterpret_cast<CUiMesh*>(sceneObjects_.data_[0])->visible_ = 0;
        regions_.data_[0]->SetTip(0, 1, 0);
    }

    // The quests: the player's variables "q.<zone>.<quest>" that are not 0, sorted by name.
    auto* quests = reinterpret_cast<CStringArray*>(quests_);
    quests->SetSize(0, -1);
    {
        mfc::CString prefix;
        CStringArray parts;
        alignas(VariableTable) unsigned char tableStorage[sizeof(VariableTable)];
        auto* table = new (tableStorage) VariableTable();
        prefix.Format(GameText(0x00794140), shownZone_);   // "q.%s"
        table->Fill(prefix);
        StringVector found = {nullptr, nullptr, nullptr};
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            if (node->value == 0.0f || x87::IsNaN(node->value)) continue;
            StringSplit(node->key, &parts, '.');
            if (parts.GetSize() == 3) PushBack(&found, parts[2]);
        }
        shownQuest_ = -1;
        selectedQuest_ = 0;
        if (found.Size() == 0) {
            selectedQuest_ = -1;
            LastQuest().Empty();
        } else {
            SortStrings(found.begin, found.end);
            for (mfc::CString* s = found.begin; s != found.end; ++s) {
                quests->SetAtGrow(quests->GetSize(), *s);
                const char* current = listZones_ == 0 ? static_cast<const char*>(LastQuest())
                                                      : *reinterpret_cast<char* const*>(
                                                            reinterpret_cast<std::uint8_t*>(interface_) + 0xAC);
                if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(current),
                                     reinterpret_cast<const unsigned char*>(static_cast<const char*>(*s))) == 0)
                    selectedQuest_ = quests->GetSize() - 1;
            }
            LastQuest() = (*quests)[selectedQuest_];
        }
        int rows = quests->GetSize();
        reinterpret_cast<ScrollbarAt*>(bar_)->SetRange(rows > 5 ? rows - 5 : 0);
        bar_->CallSetPosition(0);
        unknown_1C0 = 0;
        RefreshQuests();

        // The zone's title and description ("zone <name>") on surface 1.
        {
            mfc::CString text;    // (declared first: the original releases the title first)
            mfc::CString title;
            int foundText;
            {
                mfc::CString zoneKey(GameText(0x007941F8));   // "zone "
                mfc::CString key = zoneKey + *reinterpret_cast<const mfc::CString*>(&shownZone_);
                foundText = FindText(key);
            }
            if (foundText) {
                Texts()->ReadLine(&title, 1);
                Texts()->ReadToHash(&text, 1);
                auto* surface = reinterpret_cast<SurfaceDraw*>(surfaces_.data_[1]);
                surface->Begin();
                surface->Clear();
                RECT titleRect = {0, 5, 340, 25};
                surface->Text(title, &titleRect, 1, 0x31B3FF, 1, 1);
                RECT textRect = {10, 25, 330, 140};
                surface->Text(text, &textRect, 1, 0xA7D7E4, 0x11, 1);
                surface->End();
            }
        }

        for (mfc::CString* s = found.begin; s != found.end; ++s) s->~CString();
        if (found.begin)
            stlp::deallocate(found.begin,
                             static_cast<std::size_t>(found.capacityEnd - found.begin) * sizeof(mfc::CString));
        table->~VariableTable();
    }
}

// --- 0064DC60: the selected quest ------------------------------------------------------------------

namespace {

// The text reader's line steps (text_reader.cpp; called through their addresses).
__declspec(naked) BOOL __fastcall ReaderNextContentLine(CTextManager*) EI_ORIGINAL(0x004DF510)
__declspec(naked) BOOL __fastcall ReaderNextLine(CTextManager*, void*, int) EI_ORIGINAL(0x004DF030)
__declspec(naked) int __fastcall ReaderSplit(CTextManager*, void*, CStringArray*, int) EI_ORIGINAL(0x004DFCB0)
__declspec(naked) BOOL __fastcall ReaderClose(CTextManager*) EI_ORIGINAL(0x004DF020)
// The quest log (the window's first child) [thiscall]: clear [0065D450], set the text [0065CFE0].
__declspec(naked) void __fastcall LogClear(CUiWindow*) EI_ORIGINAL(0x0065D450)
__declspec(naked) void __fastcall LogSetText(CUiWindow*, void*, const char*) EI_ORIGINAL(0x0065CFE0)
// 0064EA80 [thiscall on the window]: fills the quest places (+0x1CC) of quest `name`.
__declspec(naked) void __fastcall FillQuestPlaces(CObjectivesWindow*, void*, const char*) EI_ORIGINAL(0x0064EA80)
// The quest database's index (007C87B4, map<string, record*>): operator[] [0064EE90], creating an
// empty slot for a name it lacks.
void* QuestIndex() { return reinterpret_cast<void*>(0x007C87B4); }
// STLport string members: default constructor [004217E0], assign [00428F10], destructor [0040C720].

// The quest's experience (+0xC) and money (+0x14) rewards, from the quest database.
void* QuestRecord(const char* name) {
    std::uint32_t key[3];
    std::uint8_t allocator = 0;
    StringInitWithAllocator(key, nullptr, &allocator);
    StringAssignSpan(key, nullptr, name, name + std::strlen(name));
    void* record = *QuestIndexAt(QuestIndex(), nullptr, key);
    StlStringDestroy(key);
    return record;
}

bool SameText(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
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
    LogClear(children_.data_[0]);
    auto* quests = reinterpret_cast<CStringArray*>(quests_);
    if (selectedQuest_ < 0 || selectedQuest_ >= quests->GetSize()) return;
    LastQuest() = (*quests)[selectedQuest_];

    mfc::CString completed;
    mfc::CString failed;
    mfc::CString costExp;
    mfc::CString costMoney;
    texts->GetString(GameText(0x0079472C), &completed);   // "obj_completed"
    texts->GetString(GameText(0x00794720), &failed);      // "obj_failed"
    texts->GetString(GameText(0x00794710), &costExp);     // "obj_cost_exp"
    texts->GetString(GameText(0x00794700), &costMoney);   // "obj_cost_money"
    bool found;
    {
        mfc::CString questWord(GameText(0x00790790));     // "quest "
        mfc::CString key = questWord + (*quests)[selectedQuest_];
        found = FindText(key) != 0;
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    ReaderNextContentLine(texts);
    texts->ReadToHash(&text, 1);
    {
        mfc::CString colour(GameText(0x007946F8));
        text = colour + text;
    }
    text += GameText(0x007946F4);
    if (void* record = QuestRecord((*quests)[selectedQuest_])) {
        float experience = *reinterpret_cast<const float*>(static_cast<char*>(record) + 0xC);
        text += GameText(0x007946EC);
        if (experience > 0.0f) {   // 0073B858
            part.Format(GameText(0x007946E4), static_cast<const char*>(costExp), GameFtol(D(experience)));   // "%s %d\n"
            text += part;
        }
        float money = *reinterpret_cast<const float*>(static_cast<char*>(record) + 0x14);
        if (money > 0.0f) {
            part.Format(GameText(0x007946E4), static_cast<const char*>(costMoney), GameFtol(D(money)));
            text += part;
        }
    }
    while (ReaderNextLine(texts, nullptr, 0)) {
        ReaderSplit(texts, nullptr, &fields, 1);
        if (fields.GetSize() < 2) continue;
        if (SameText(fields[0], GameText(0x007946DC))) break;      // "#quest"
        if (!SameText(fields[0], GameText(0x007946D4))) continue;  // "#subobj"
        variable.Format(GameText(0x007946C8), shownZone_, static_cast<const char*>((*quests)[selectedQuest_]),
                        static_cast<const char*>(fields[1]));   // "q.%s.%s.%s"
        int state = GameFtol(D(LocalPlayerVariable(variable)));
        if (state == 0) continue;
        texts->ReadLine(&line, 1);
        text += GameText(0x0079349C);
        text += line;
        text += GameText(0x0078DA40);   // " "
        if (state == 2) {
            text += GameText(0x007946B8);
            text += completed;
        } else if (state == 3) {
            text += GameText(0x007946C0);
            text += failed;
        }
        text += GameText(0x007946AC);
        texts->ReadToHash(&line, 1);
        text += line;
        text += GameText(0x007946A4);
    }
    ReaderClose(texts);
    LogSetText(children_.data_[0], nullptr, text);

    // The quest's places on the zone picture: a red marker and a region each, after the exits'.
    ZoneView* map = FindZone(shownZone_);
    if (!map) return;
    FillQuestPlaces(this, nullptr, (*quests)[selectedQuest_]);
    while (sceneObjects_.size_ > exitCount_ + 0x15) RemoveSceneObjectAt(exitCount_ + 0x15);
    while (regions_.size_ > exitCount_ + 9) RemoveRegionAt(exitCount_ + 9);
    const float red[3] = {1.0f, 0.0f, 0.0f};
    for (unsigned i = 0; i < static_cast<unsigned>(questPlacesEnd_ - questPlacesBegin_); ++i) {
        float worldWidth = Store(static_cast<double>(map->width << 5));
        float worldHeight = Store(static_cast<double>(map->height << 5));
        float span = worldWidth < worldHeight || x87::IsNaN(worldWidth) || x87::IsNaN(worldHeight) ? worldHeight
                                                                                                   : worldWidth;
        const UiPointF& place = questPlacesBegin_[i];
        float x = Store((D(place.x) - D(worldWidth) * 0.5f) * kPictureSpan / D(span) + kPictureCentreX);
        float y = Store(kPictureCentreY - (D(place.y) - D(worldHeight) * 0.5f) * kPictureSpan / D(span));
        CUiMesh* marker = AddSprite(0);
        float bottom = Store(D(y) + kMarkerHalfWidth);
        float right = Store(D(x) + kMarkerHalfWidth);
        float top = Store(D(y) - kMarkerHalfWidth);
        float left = Store(D(x) - kMarkerHalfWidth);
        marker->SetRectangle(5.0f, 100.0f, left, top, right, bottom, 0, 0, 0x80, 0x80);
        SetSpriteTexture(marker, objects_.data_[1]);
        std::memcpy(reinterpret_cast<std::uint8_t*>(marker) + 0x224, red, sizeof(red));
        CUiRegion* region = AddRegion();
        int bottomPixel = GameFtol(D(bottom));
        int rightPixel = GameFtol(D(right));
        int topPixel = GameFtol(D(top));
        int leftPixel = GameFtol(D(left));
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
    auto* objectiveTexts = reinterpret_cast<CStringArray*>(objectiveTexts_);
    for (int k = 0; k < 3; ++k) {
        key.Format(GameText(0x00794670), k);   // "obj_%d"
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

    LoadTexture(GameText(0x00793380), 3, 0);   // "saveload"
    LoadTexture(GameText(0x00794668), 3, 0);   // "cross"
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
        SetSpriteTexture(mark, objects_.data_[1]);
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
    unknown_1C8 = 0;

    if (listZones_ == 0) {
        ShowZone(zoneName_);
    } else {
        ZoneView* allodZone = FindZone(zoneName_);
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
        table->Fill(GameText(0x00783D7C));   // "q."
        auto* zones = reinterpret_cast<StringVector*>(&zonesBegin_);
        const char* interfaceQuest = *reinterpret_cast<char* const*>(reinterpret_cast<std::uint8_t*>(interface_) + 0xAC);
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            StringSplit(node->key, &parts, '.');
            if (parts.GetSize() != 3) continue;
            if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(interfaceQuest),
                                 reinterpret_cast<const unsigned char*>(static_cast<const char*>(parts[2]))) == 0)
                current = parts[1];
            std::uint32_t one;
            std::memcpy(&one, &node->value, 4);
            if (one != 0x3F800000) continue;
            ZoneView* zone = FindZone(parts[1]);
            if (!zone) continue;
            if (!IsMultiplayerClient() &&
                gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(zone->allod),
                                 reinterpret_cast<const unsigned char*>(allodZone->allod)) != 0)
                continue;
            bool listed = false;
            for (mfc::CString* z = zones->begin; z != zones->end; ++z)
                if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(zone->name),
                                    reinterpret_cast<const unsigned char*>(static_cast<const char*>(*z))) == 0) {
                    listed = true;
                    break;
                }
            if (!listed) PushBack(zones, *reinterpret_cast<const mfc::CString*>(&zone->name));
        }
        if (zones->Size() == 0 && reinterpret_cast<const int*>(static_cast<const char*>(current))[-2] != 0)
            PushBack(zones, current);
        if (zones->Size() == 0) {
            interface_->SetPendingValue(0);
            table->~VariableTable();
            return;
        }
        zoneIndex_ = 0;
        for (int i = 0; i < zones->Size(); ++i)
            if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(current)),
                                 reinterpret_cast<const unsigned char*>(static_cast<const char*>(zones->begin[i]))) ==
                0) {
                zoneIndex_ = i;
                break;
            }
        if (zones->Size() > 1) {
            reinterpret_cast<CUiMesh*>(sceneObjects_.data_[14])->visible_ = 1;
            reinterpret_cast<CUiMesh*>(sceneObjects_.data_[15])->visible_ = 1;
            regions_.data_[2]->SetTip(0x13948, 1, 0);
            regions_.data_[3]->SetTip(0x13949, 1, 0);
        }
        ShowZone(zones->begin[zoneIndex_]);
        table->~VariableTable();
    }
    interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x0064BE80, CObjectivesWindow::Build);
EI_REPLACE_METHOD(0x0064CC10, CObjectivesWindow::ShowZone);
EI_REPLACE_METHOD(0x0064DC60, CObjectivesWindow::ShowQuest);
