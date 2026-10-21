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
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_engine.h"
#include "game_data.h"
#include "game_singletons.h"

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
MapRecord* FindZone(const char* name) { return TheMapDatabase()->FindMap(name); }

struct UiTexts {
    // 004DEFC0 (not rewritten). Finds text `key`; non-zero when found (the cursor at its start).
    int Find(const char* key);
};
int UiTexts::Find(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
int FindText(const char* key) { return reinterpret_cast<UiTexts*>(TheTextManager())->Find(key); }

// The quest the window last selected (a global CString).
mfc::CString& LastQuest() { return ei::GameData<mfc::CString>(0x007C8178); }

// 004DBE40 (not rewritten): splits `text` at `separator` into `out`.

// A local hash_map<CString, float> filled with the player's variables starting with `prefix`.
class VariableTable {
public:
    VariableTable() { table_.ConstructDefault(); }   // 00609C10 with 100 buckets
    void Fill(const char* prefix) { GameLocalPlayerVariables(prefix, &table_); }
    ~VariableTable() { table_.Destroy(); }   // inlined: every node's key released, then the buckets

    NamedFloatTable::Node* First() const { return table_.First(); }
    NamedFloatTable::Node* Next(const NamedFloatTable::Node* node) const { return table_.Next(node); }

private:
    NamedFloatTable table_;
};

using ZoneNames = stlp::vector<mfc::CString>;
int SizeOf(const ZoneNames& v) { return static_cast<int>(v.end() - v.begin()); }
// push_back as the original inlines it (the instantiation's own overflow).
void PushBack(ZoneNames* v, const mfc::CString& x) {
    if (v->Finish() != v->EndOfStorage()) {
        new (v->Finish()) mfc::CString(x);
        ++v->Finish();
    } else {
        v->InsertOverflow<stlp::ObjectElement<mfc::CString>>(v->Finish(), x, 1);   // 0051F790
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
float ExitTurn() { return ei::GameData<const float>(0x007C8170); }   // a global angle
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
    float larger = width < height ? height : width;
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
        if (gamecrt::mbsicmp(source.target,
                            "none") == 0)
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
                    EI_NAN_AUDIT(node->second, nearest);
                    if (node->second < nearest) {
                        nearestExit_ = index;
                        std::memcpy(&nearest, &node->second, 4);
                    }
                    break;
                }
                node = DistanceMap::Next(node);
            }
        } else {
            mfc::CString name;
            name.Format("z.%s", source.target);
            float value = LocalPlayerVariable(name);
            EI_NAN_AUDIT(value);
            if (value != 1.0f) exit->flag_ = true;
        }

        // Position on the picture (world cells are 32 units).
        float worldWidth = static_cast<float>(static_cast<double>(map->sizeX << 5));
        float worldHeight = static_cast<float>(static_cast<double>(map->sizeY << 5));
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
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
        prefix.Format("q.%s", shownZone_);
        table->Fill(prefix);
        ZoneNames found;
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            EI_NAN_AUDIT(node->value.mapped);
            if (node->value.mapped == 0.0f) continue;
            text::Split(node->value.key, &parts, '.');
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
                if (gamecrt::mbsicmp(current,
                                    static_cast<const char*>(*s)) == 0)
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
                mfc::CString zoneKey("zone ");
                mfc::CString key = zoneKey + *reinterpret_cast<const mfc::CString*>(&shownZone_);
                foundText = FindText(key);
            }
            if (foundText) {
                TheTextManager()->ReadLine(&title, 1);
                TheTextManager()->ReadToHash(&text, 1);
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
QuestDatabase::QuestIndex& QuestIndex() { return ei::GameData<QuestDatabase::QuestIndex>(0x007C87B4); }

// The quest's record (its experience and money rewards), from the quest database.
QuestRecord* FindQuestRecord(const char* name) {
    stlp::string key(name);
    return QuestIndex().Subscript(key);
}

bool SameText(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b) == 0;
}

} // namespace

void CObjectivesWindow::ShowQuest() {
    CTextManager* texts = TheTextManager();
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
    texts->GetString("obj_completed", &completed);
    texts->GetString("obj_failed", &failed);
    texts->GetString("obj_cost_exp", &costExp);
    texts->GetString("obj_cost_money", &costMoney);
    bool found;
    {
        mfc::CString questWord("quest ");
        mfc::CString key = questWord + (*quests)[selectedQuest_];
        found = FindText(key) != 0;
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    TextNextLine(texts);
    texts->ReadToHash(&text, 1);
    {
        mfc::CString colour("\007C\344\327\247 ");
        text = colour + text;
    }
    text += "\n ";
    if (QuestRecord* record = FindQuestRecord((*quests)[selectedQuest_])) {
        float experience = record->exp;
        text += "\007C\377\377\377 ";
        if (experience > 0.0f) {   // 0073B858
            part.Format("%s %d\n ", static_cast<const char*>(costExp), static_cast<int>(gamecrt::Ftol(D(experience))));   // "%s %d\n"
            text += part;
        }
        float money = record->money;
        if (money > 0.0f) {
            part.Format("%s %d\n ", static_cast<const char*>(costMoney), static_cast<int>(gamecrt::Ftol(D(money))));
            text += part;
        }
    }
    while (TextFindLineOriginal(texts, nullptr, nullptr)) {
        texts->Split(&fields, 1);
        if (fields.GetSize() < 2) continue;
        if (SameText(fields[0], "#quest")) break;
        if (!SameText(fields[0], "#subobj")) continue;
        variable.Format("q.%s.%s.%s", shownZone_, static_cast<const char*>((*quests)[selectedQuest_]),
                        static_cast<const char*>(fields[1]));   // "q.%s.%s.%s"
        int state = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable(variable))));
        if (state == 0) continue;
        texts->ReadLine(&line, 1);
        text += "\007C\377\263" "1 ";
        text += line;
        text += " ";
        if (state == 2) {
            text += "\007Ct\334\001 ";
            text += completed;
        } else if (state == 3) {
            text += "\007C\377`H ";
            text += failed;
        }
        text += "\n \n \007C\344\327\247";
        texts->ReadToHash(&line, 1);
        text += line;
        text += "\n \n ";
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
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
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
        key.Format("obj_%d", k);
        TheTextManager()->GetString(key, &text);
        objectiveTexts->SetAtGrow(objectiveTexts->GetSize(), text);
    }

    // The quest log (left, below the list) and the list's scroll bar.
    void* memory = gamecrt::operator_new(0x1C8);
    AddChild(memory ? static_cast<CUiScrollText*>(memory)->Construct(interface_, static_cast<const char*>(game::EmptyText()), RECT{60, 190, 384, 510}, 10.0f)
                    : nullptr);
    memory = gamecrt::operator_new(0x18C);
    bar_ = memory ? static_cast<CUiScrollbar*>(memory)->Construct(interface_, RECT{60, 30, 384, 150}, 3.0f, 0, 1.0f)
                  : nullptr;
    AddChild(bar_);

    LoadTexture("saveload", 3, 0);
    LoadTexture("cross", 3, 0);
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
        table->Fill("q.");
        ZoneNames* zones = &zones_;
        const char* interfaceQuest = interface_->savedText_;
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            text::Split(node->value.key, &parts, '.');
            if (parts.GetSize() != 3) continue;
            if (gamecrt::mbsicmp(interfaceQuest,
                                static_cast<const char*>(parts[2])) == 0)
                current = parts[1];
            std::uint32_t one;
            std::memcpy(&one, &node->value.mapped, 4);
            if (one != 0x3F800000) continue;
            MapRecord* zone = FindZone(parts[1]);
            if (!zone) continue;
            if (!IsMultiplayerClient() &&
                gamecrt::mbsicmp(zone->allod,
                                allodZone->allod) != 0)
                continue;
            bool listed = false;
            for (mfc::CString* z = zones->begin(); z != zones->end(); ++z)
                if (gamecrt::mbscmp(zone->name,
                                    static_cast<const char*>(*z)) == 0) {
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
            if (gamecrt::mbsicmp(static_cast<const char*>(current),
                                static_cast<const char*>(zones->begin()[i])) ==
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
    interface_->OpenHelp(0);
}
void CObjectivesWindow::DrawQuestList() {
    CTextManager* texts = TheTextManager();
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
            mfc::CString questWord("quest ");
            mfc::CString key = questWord + (*quests)[row];
            found = FindText(key) != 0;
        }
        if (found) texts->ReadLine(&title, 1);
        else title = (*quests)[row];
        variable.Format("q.%s.%s", shownZone_, static_cast<const char*>((*quests)[row]));
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
const char* const kGlobalMapTopic = "quest_global_map";
const char* const kMissionTopic = "quest_mission";
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
