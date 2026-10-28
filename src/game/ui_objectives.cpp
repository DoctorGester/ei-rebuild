// The objectives window (CObjectivesWindow): its Create, the zone page and the selected quest's description
// and places. TAB in a zone opens it for the current zone; opened with `listZones_` it lists the zones of the
// zone's allod in which the player has quests and shows one of them. See docs/notes/ui.md.
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

#include "lib/stlp_hash.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>


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
MapRecord* FindZone(const std::string& name) { return FindZone(name.c_str()); }

int FindText(const char* key) { return TheTextManager()->Open(key); }

// The quest the window last selected (a global CString): built empty by its static initialiser in the list's
// order, destroyed once by the exit function it registers. Both are defined on their names
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<std::string> g_lastQuest = /*007C8178*/ {};
constinit std::uint8_t g_lastQuestDestroyed = /*007C8174*/ 0;
std::string& LastQuest() { return *g_lastQuest; }

void DestroyLastQuest() {
    if (g_lastQuestDestroyed & 1) return;
    g_lastQuestDestroyed = static_cast<std::uint8_t>(g_lastQuestDestroyed | 1);
    g_lastQuest.Destroy();
}
class VariableTable {
public:
    VariableTable() { table_.ConstructDefault(); }   // with 100 buckets
    void Fill(const char* prefix) { GameLocalPlayerVariables(prefix, &table_); }
    ~VariableTable() { table_.Destroy(); }   // every node's key released, then the buckets

    NamedFloatTable::Node* First() const { return table_.First(); }
    NamedFloatTable::Node* Next(const NamedFloatTable::Node* node) const { return table_.Next(node); }

private:
    NamedFloatTable table_;
};

using ZoneNames = stlp::vector<std::string>;
int SizeOf(const ZoneNames& v) { return static_cast<int>(v.end() - v.begin()); }
void PushBack(ZoneNames* v, const std::string& x) {
    if (v->Finish() != v->EndOfStorage()) {
        std::construct_at(v->Finish(), x);
        ++v->Finish();
    } else {
        v->InsertOverflow<stlp::ObjectElement<std::string>>(v->Finish(), x, 1);
    }
}

void SortStrings(std::string* first, std::string* last) {
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

using DistanceMap = stlp::map<int, float>;
using DistanceNode = DistanceMap::Node;

int SurfaceText(CUiSurface* surface, std::string_view text, RECT* rect, int font, std::uint32_t colour, int format, int flag) {
    return surface->DrawTextIn(text, rect, font, colour, format, flag);
}

void SetSpriteTexture(CUiMesh* sprite, CTexture* texture) { sprite->texture_ = texture; }
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
    region->SetRect(left, top, right, bottom);
    return region;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
        return new CUiSurface(ui, left, top, right, bottom, 5, 1);
}

void SetRenderState(CTextureDevice* device, int state, std::uint32_t value) {
    uidevice::SetRenderState(device, static_cast<std::uint32_t>(state), value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
void GetRenderState(CTextureDevice* device, int state, std::uint32_t* value) {
    uidevice::GetRenderState(device, static_cast<std::uint32_t>(state), value);
}

// Layout constants (zone picture centre and size on the right half of the window).
constexpr float kPictureCentreX = 580.0f;
constexpr float kPictureCentreY = 190.0f;
constexpr float kPictureHalf = 150.0f;
constexpr float kTexels = 256.0f;
constexpr float kPictureSpan = 300.0f;
constexpr float kMarkerHeight = 20.0f;
constexpr float kMarkerHalfWidth = 10.0f;
constexpr float kExitHalf = 15.0f;
constexpr std::uint32_t kFarAway = 0x5A0E1BCA;   // 1e16
constexpr float kExitTurn = /*007C8170*/ 1.5707964f;   // a static initialiser: pi * 0.5
constexpr float kExitTurnDown = 3.1415927f;

} // namespace

void ConstructLastQuest() {
    g_lastQuest.Construct();
    libcrt::AtExit(DestroyLastQuest);
}

// --- The zone page ----------------------------------------------------------------------

void CObjectivesWindow::ShowZone(const char* zone) {
    shownZone_ = zone;
    while (objects_.size > 2) RemoveObjectAt(2);
    while (sceneObjects_.size > 0x15) RemoveSceneObjectAt(0x15);
    while (regions_.size > 9) RemoveRegionAt(9);

    MapRecord* map = FindZone(shownZone_);
    if (!map) {
        interface_->SetPendingValue(0);
        return;
    }

    LoadTexture(map->picture2.c_str(), 3, 0);
    float width = static_cast<float>(static_cast<double>(map->sizeX));
    float height = static_cast<float>(static_cast<double>(map->sizeY));
    float larger = width < height ? height : width;
    double widthShare = D(width) / D(larger);
    float heightShare = static_cast<float>(D(height) / D(larger));
    float halfHeight = static_cast<float>(D(heightShare) * kPictureHalf);
    float halfWidth = static_cast<float>(widthShare * kPictureHalf);
    int u1 = static_cast<int>(gamecrt::Ftol(widthShare * kTexels));
    int v0 = static_cast<int>(gamecrt::Ftol((1.0f - D(heightShare)) * kTexels));
    CUiMesh* picture = SceneMesh(3);
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
        const ZoneExit& source = map->exits.data[i];
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
            std::string name;
            name = str::Format("z.%s", source.target.c_str());
            float value = LocalPlayerVariable(name.c_str());
            EI_NAN_AUDIT(value);
            if (value != 1.0f) exit->flag_ = true;
        }

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
            Turn(marker, -kExitTurn);
            gameapi::OffsetRect(&rect, 10, 0);
        } else {
            Turn(marker, kExitTurn);
            gameapi::OffsetRect(&rect, -10, 0);
        }
        region->SetRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    SelectExit(nearestExit_);
    if (twoButtons_ != 0 && !anyKnown) {
        SceneMesh(0)->visible_ = 0;
        regions_.data[0]->SetTip(0, 1, 0);
    }

    std::vector<std::string>& quests = quests_;
    quests.clear();
    {
        std::string prefix;
        std::vector<std::string> parts;
        alignas(VariableTable) unsigned char tableStorage[sizeof(VariableTable)];
        auto* table = new (tableStorage) VariableTable();
        prefix = str::Format("q.%s", shownZone_.c_str());
        table->Fill(prefix.c_str());
        ZoneNames found;
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            EI_NAN_AUDIT(node->value.mapped);
            if (node->value.mapped == 0.0f) continue;
            text::Split(node->value.key, &parts, '.');
            if (parts.size() == 3) PushBack(&found, parts[2].c_str());
        }
        shownQuest_ = -1;
        selectedQuest_ = 0;
        if (SizeOf(found) == 0) {
            selectedQuest_ = -1;
            LastQuest().clear();
        } else {
            SortStrings(found.begin(), found.end());
            for (std::string* s = found.begin(); s != found.end(); ++s) {
                quests.push_back(s->c_str());
                const char* current = listZones_ == 0 ? LastQuest().c_str()
                                                      : interface_->savedText_.c_str();
                if (gamecrt::mbsicmp(current,
                                    s->c_str()) == 0)
                    selectedQuest_ = static_cast<int>(quests.size()) - 1;
            }
            LastQuest() = quests[selectedQuest_];
        }
        int rows = static_cast<int>(quests.size());
        bar_->SetMaximum(rows > 5 ? rows - 5 : 0);
        bar_->SetPosition(0);
        firstRow_ = 0;
        RefreshQuests();

        {
            std::string text; // (declared first: the title is released first)
            std::string title;
            int foundText;
            {
                std::string zoneKey("zone ");
                std::string key = zoneKey + shownZone_;
                foundText = FindText(key.c_str());
            }
            if (foundText) {
                TheTextManager()->ReadLine(&title, 1);
                TheTextManager()->ReadToHash(&text, 1);
                CUiSurface* surface = surfaces_.data[1];
                UiSurfaceGetDC(surface);
                surface->Clear();
                RECT titleRect = {0, 5, 340, 25};
                SurfaceText(surface, title, &titleRect, 1, 0x31B3FF, 1, 1);
                RECT textRect = {10, 25, 330, 140};
                SurfaceText(surface, text, &textRect, 1, 0xA7D7E4, 0x11, 1);
                UiSurfaceReleaseDC(surface);
            }
        }

        for (std::string* s = found.begin(); s != found.end(); ++s) std::destroy_at(s);
        found.FreeStorage();
        table->~VariableTable();
    }
}

// --- The selected quest ------------------------------------------------------------------

namespace {

QuestDatabase::QuestIndex& QuestIndex() { return TheQuestDatabase().questsByName; }

QuestRecord* FindQuestRecord(const char* name) {
    std::string key(name);
    return QuestIndex().Subscript(key);
}

bool SameText(std::string_view a, std::string_view b) {
    return gamecrt::mbsicmp(a, b) == 0;
}

} // namespace

void CObjectivesWindow::ShowQuest() {
    CTextManager* texts = TheTextManager();
    std::string line;
    std::string part;
    std::string text;
    std::vector<std::string> fields;
    std::string variable;
    if (shownQuest_ == selectedQuest_) return;
    shownQuest_ = selectedQuest_;
    static_cast<CUiScrollText*>(children_.data[0])->Clear();
    std::vector<std::string>& quests = quests_;
    if (selectedQuest_ < 0 || selectedQuest_ >= static_cast<int>(quests.size())) return;
    LastQuest() = quests[selectedQuest_];

    std::string completed;
    std::string failed;
    std::string costExp;
    std::string costMoney;
    texts->GetString("obj_completed", &completed);
    texts->GetString("obj_failed", &failed);
    texts->GetString("obj_cost_exp", &costExp);
    texts->GetString("obj_cost_money", &costMoney);
    bool found;
    {
        std::string questWord("quest ");
        std::string key = questWord + quests[selectedQuest_];
        found = FindText(key.c_str()) != 0;
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    TextNextLine(texts);
    texts->ReadToHash(&text, 1);
    {
        std::string colour("\007C\344\327\247 ");
        text = colour + text;
    }
    text += "\n ";
    if (QuestRecord* record = FindQuestRecord(quests[selectedQuest_].c_str())) {
        float experience = record->exp;
        text += "\007C\377\377\377 ";
        if (experience > 0.0f) {
            part = str::Format("%s %d\n ", costExp.c_str(), static_cast<int>(gamecrt::Ftol(D(experience))));
            text += part;
        }
        float money = record->money;
        if (money > 0.0f) {
            part = str::Format("%s %d\n ", costMoney.c_str(), static_cast<int>(gamecrt::Ftol(D(money))));
            text += part;
        }
    }
    while (texts->FindLine(nullptr)) {
        texts->Split(&fields, 1);
        if (fields.size() < 2) continue;
        if (SameText(fields[0], "#quest")) break;
        if (!SameText(fields[0], "#subobj")) continue;
        variable = str::Format("q.%s.%s.%s", shownZone_.c_str(), quests[selectedQuest_].c_str(),
                        fields[1].c_str());
        int state = static_cast<int>(gamecrt::Ftol(D(LocalPlayerVariable(variable.c_str()))));
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
    static_cast<CUiScrollText*>(children_.data[0])->SetText(text.c_str());

    MapRecord* map = FindZone(shownZone_);
    if (!map) return;
    LoadQuestPlaces(quests[selectedQuest_].c_str());
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
        region->SetRect(leftPixel, topPixel, rightPixel, bottomPixel);
        region->SetTip(0x138F0, 1, 0);
    }
}

// --- Create ------------------------------------------------------------------------------

void CObjectivesWindow::CreateScreen() {
    CTextureDevice* device = interface_->renderer_;
    GetRenderState(device, kRenderStateAlphaRef, &savedAlphaRef_);
    SetRenderState(device, kRenderStateAlphaRef, 1);

    std::string text;
    std::string key;
    for (int k = 0; k < 3; ++k) {
        key = str::Format("obj_%d", k);
        TheTextManager()->GetString(key.c_str(), &text);
        objectiveTexts_.push_back(text);
    }

    AddChild(new CUiScrollText(interface_, game::EmptyText(), RECT{60, 190, 384, 510}, 10.0f));
    bar_ = new CUiScrollbar(interface_, RECT{60, 30, 384, 150}, 3.0f, 0, 1.0f);
    AddChild(bar_);

    LoadTexture("saveload", 3, 0);
    LoadTexture("cross", 3, 0);
    AddSurface(NewSurface(interface_, 50, 20, 390, 160));
    AddSurface(NewSurface(interface_, 410, 380, 750, 520));
    AddSurface(NewSurface(interface_, 280, 540, 520, 580));

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
    CUiMesh* left = AddSprite(0);
    left->SetRectangle(3.0f, 100.0f, 414.0f, 384.0f, 443.0f, 404.0f, 2, 0x72, 0x1F, 0x86);
    left->visible_ = 0;
    CUiMesh* right = AddSprite(0);
    right->SetRectangle(3.0f, 100.0f, 717.0f, 384.0f, 746.0f, 404.0f, 0x21, 0x72, 0x3E, 0x86);
    right->visible_ = 0;
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
        ShowZone(zoneName_.c_str());
    } else {
        MapRecord* allodZone = FindZone(zoneName_);
        if (!allodZone) {
            interface_->SetPendingValue(0);
            return;
        }
        // The zones of this allod in which the player holds quests ("q.<zone>.<quest>" = 1), and
        // the zone the interface's current quest belongs to.
        std::string current;
        std::vector<std::string> parts;
        alignas(VariableTable) unsigned char tableStorage[sizeof(VariableTable)];
        auto* table = new (tableStorage) VariableTable();
        table->Fill("q.");
        ZoneNames* zones = &zones_;
        const char* interfaceQuest = interface_->savedText_.c_str();
        for (NamedFloatTable::Node* node = table->First(); node; node = table->Next(node)) {
            text::Split(node->value.key, &parts, '.');
            if (parts.size() != 3) continue;
            if (gamecrt::mbsicmp(interfaceQuest,
                                parts[2].c_str()) == 0)
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
            for (std::string* z = zones->begin(); z != zones->end(); ++z)
                if (gamecrt::mbscmp(zone->name,
                                    z->c_str()) == 0) {
                    listed = true;
                    break;
                }
            if (!listed) PushBack(zones, zone->name);
        }
        if (SizeOf(*zones) == 0 && static_cast<int>(current.size()) != 0)
            PushBack(zones, current);
        if (SizeOf(*zones) == 0) {
            interface_->SetPendingValue(0);
            table->~VariableTable();
            return;
        }
        zoneIndex_ = 0;
        for (int i = 0; i < SizeOf(*zones); ++i)
            if (gamecrt::mbsicmp(current,
                                zones->begin()[i].c_str()) ==
                0) {
                zoneIndex_ = i;
                break;
            }
        if (SizeOf(*zones) > 1) {
            SceneMesh(14)->visible_ = 1;
            SceneMesh(15)->visible_ = 1;
            regions_.data[2]->SetTip(0x13948, 1, 0);
            regions_.data[3]->SetTip(0x13949, 1, 0);
        }
        ShowZone(zones->begin()[zoneIndex_].c_str());
        table->~VariableTable();
    }
    interface_->OpenHelp(0);
}
void CObjectivesWindow::DrawQuestList() {
    CTextManager* texts = TheTextManager();
    std::string title;
    std::string variable;
    auto mesh = [this](int index) { return SceneMesh(index); };
    mesh(2)->visible_ = 0;
    auto* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    for (int i = 16; i < 21; ++i) mesh(i)->visible_ = 0;
    std::vector<std::string>& quests = quests_;
    int row = firstRow_;
    int y = 13;
    for (int i = 16; i < 21 && row < static_cast<int>(quests.size()); ++i, ++row, y += 24) {
        CUiMesh* mark = mesh(i);
        mark->visible_ = 1;
        bool found;
        {
            std::string questWord("quest ");
            std::string key = questWord + quests[row];
            found = FindText(key.c_str()) != 0;
        }
        if (found) texts->ReadLine(&title, 1);
        else title = quests[row];
        variable = str::Format("q.%s.%s", shownZone_.c_str(), quests[row].c_str());
        int state = static_cast<int>(LocalPlayerVariable(variable.c_str()));
        if (state == 2) mark->textureOffsetU_ = 0.265625f;
        else if (state == 3) mark->textureOffsetU_ = 0.0f;
        else mark->visible_ = 0;
        if (row == selectedQuest_) {
            mesh(2)->PlaceBody(215.0f, static_cast<float>(y + 29), 3.0f);
            mesh(2)->visible_ = 1;
        }
        RECT rect{36, y, 320, y + 21};
        surface->DrawTextIn(title.c_str(), &rect, 1, 0xA7D7E4, 0, 1);
    }
    UiSurfaceReleaseDC(surface);
}

// --- What the window is opened for, and its help topic --------------------

namespace {
const char* const kGlobalMapTopic = "quest_global_map";
const char* const kMissionTopic = "quest_mission";
} // namespace

void CObjectivesWindow::Setup(const char* zone, void* exitDistances, int twoButtons, int listZones) {
    zoneName_ = zone;
    twoButtons_ = twoButtons;
    exitDistances_ = static_cast<stlp::map<int, float>*>(exitDistances);
    listZones_ = listZones;
}

const char* CObjectivesWindow::HelpTopic() { return twoButtons_ ? kGlobalMapTopic : kMissionTopic; }
