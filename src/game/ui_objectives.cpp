// The objectives window (CObjectivesWindow): its Create, the zone page and the selected quest's description
// and places. TAB in a zone opens it for the current zone; opened with `listZones_` it lists the zones of the
// zone's allod in which the player has quests and shows one of them. See docs/notes/ui.md.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item_db_text.h"
#include "maths.h"
#include "object_tables.h"
#include "quest_records.h"
#include "render/render_state.h"
#include "text_split.h"
#include "ui.h"
#include "ui_float.h"
#include "world.h"
#include "world_glue.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/gameapi.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/x87.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <format>

namespace {

// --- Game data ----------------------------------------------------------------------------------

// The quest the window last selected: the next window over the same zone selects it again.
std::string g_lastQuest;

// The zone's quests (the names of the "q.<zone>.<quest>" variables) by their quest record's order, the highest
// first, and a name without a record before all (also before itself, so not a strict weak ordering: the game's
// sort, whose unguarded scans can then run off the range as in the game). Every comparison looks both names up,
// the first argument's first (FindQuest adds a null entry for a new name). Quests of the same order keep the
// game's sort's order.
void SortQuests(std::string* first, std::string* last) {
    lib::IntroSort(first, last, [](const std::string& a, const std::string& b) {
        const QuestRecord* quest = g_questDatabase.FindQuest(a);
        const QuestRecord* other = g_questDatabase.FindQuest(b);
        return !quest || (other && static_cast<unsigned>(quest->order) > static_cast<unsigned>(other->order));
    });
}


void Turn(CUiMesh* sprite, float angle) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    sprite->localRotation_ = q;
}

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    return new CUiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
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

// The quest database's record of `name` (the index's operator[] adds a null entry for a new name).
QuestRecord* FindQuestRecord(std::string_view name) { return g_questDatabase.questsByName[std::string(name)]; }

const char* const kGlobalMapTopic = "quest_global_map";
const char* const kMissionTopic = "quest_mission";

} // namespace

// --- The zone page ----------------------------------------------------------------------

void CObjectivesWindow::ShowZone(std::string_view zone) {
    shownZone_ = zone;
    while (std::ssize(objects_) > 2) RemoveObjectAt(2);
    while (std::ssize(sceneObjects_) > 0x15) RemoveSceneObjectAt(0x15);
    while (std::ssize(regions_) > 9) RemoveRegionAt(9);

    // The zone's picture (picture2), its size in map cells and its exits (the deploy rectangle x1, y1, x2, y2 and the
    // target zone, "none" when it leads nowhere).
    MapRecord* map = g_mapDatabase.FindMap(shownZone_);
    if (!map) {
        interface_->SetPendingValue(0);
        return;
    }

    LoadTexture(map->picture2, TextureKind::Interface);
    float width = static_cast<float>(static_cast<double>(map->sizeX));
    float height = static_cast<float>(static_cast<double>(map->sizeY));
    float larger = width < height ? height : width;
    double widthShare = D(width) / D(larger);
    float heightShare = static_cast<float>(D(height) / D(larger));
    float halfHeight = static_cast<float>(D(heightShare) * kPictureHalf);
    float halfWidth = static_cast<float>(widthShare * kPictureHalf);
    int u1 = static_cast<int>(libcrt::Ftol(widthShare * kTexels));
    int v0 = static_cast<int>(libcrt::Ftol((1.0f - D(heightShare)) * kTexels));
    CUiMesh* picture = SceneMesh(3);
    picture->SetRectangle(10.0f, 100.0f, static_cast<float>(kPictureCentreX - D(halfWidth)), static_cast<float>(kPictureCentreY - D(halfHeight)),
                          static_cast<float>(D(halfWidth) + kPictureCentreX), static_cast<float>(D(halfHeight) + kPictureCentreY), 0, v0, u1,
                          0x100);
    picture->texture_ = objects_[2];

    // The exits that lead somewhere: a marker (triangle pointing out of the picture) and a region.
    float nearest = FloatFromBits(kFarAway);
    nearestExit_ = -1;
    bool anyKnown = false;
    exits_.clear();
    for (int i = 0; i < std::ssize(map->exits); ++i) {
        const ZoneExit& source = map->exits[i];
        if (text::CompareIgnoringCase(source.target,
                            "none") == 0)
            continue;
        int index = std::ssize(exits_);
        ObjectiveExit* exit = &exits_.emplace_back();
        exit->exit_ = i;
        exit->x_ = static_cast<float>((D(source.deploy[2]) + D(source.deploy[0])) * 0.5f);
        exit->y_ = static_cast<float>((D(source.deploy[3]) + D(source.deploy[1])) * 0.5f);
        exit->distance_ = 0.0f;
        exit->flag_ = false;
        if (exitDistances_) {
            for (auto node = exitDistances_->begin(); node != exitDistances_->end(); ++node) {
                if (node->first == i) {
                    anyKnown = true;
                    exit->distance_ = node->second;
                    exit->flag_ = true;
                    EI_NAN_AUDIT(node->second, nearest);
                    if (node->second < nearest) {
                        nearestExit_ = index;
                        nearest = node->second;
                    }
                    break;
                }
            }
        } else {
            std::string name;
            name = std::format("z.{}", source.target);
            float value = LocalPlayerVariable(name);
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

        CUiMesh* marker = AddBlankSprite();
        float base = static_cast<float>(D(y) + kMarkerHeight);
        marker->SetTriangle(5.0f, 100.0f, x, y, static_cast<float>(D(x) - kMarkerHalfWidth), base, static_cast<float>(D(x) + kMarkerHalfWidth),
                            base, 240.0f, 226.0f, 227.0f, 254.0f, 253.0f, 254.0f);
        markerScale_ = marker->scaleMatrix_.m[0];
        RECT rect;
        rect.left = static_cast<int>(libcrt::Ftol(D(x) - kExitHalf));
        rect.top = static_cast<int>(libcrt::Ftol(D(y) - kExitHalf));
        rect.right = static_cast<int>(libcrt::Ftol(D(x) + kExitHalf));
        rect.bottom = static_cast<int>(libcrt::Ftol(D(y) + kExitHalf));
        CUiRegion* region = AddRegion();
        region->SetTip(0x138EE);
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
    if (twoButtons_ && !anyKnown) {
        SceneMesh(0)->visible_ = false;
        regions_[0]->SetTip(0);
    }

    std::vector<std::string>& quests = quests_;
    quests.clear();
    {
        std::string prefix;
        std::vector<std::string> parts;
        NamedFloatTable table;
        prefix = std::format("q.{}", shownZone_);
        GameLocalPlayerVariables(prefix, &table);
        std::vector<std::string> found;
        for (auto node = table.begin(); node != table.end(); ++node) {
            EI_NAN_AUDIT(node->second);
            if (node->second == 0.0f) continue;
            text::Split(node->first, &parts, '.');
            if (parts.size() == 3) found.push_back(parts[2]);
        }
        shownQuest_ = -1;
        selectedQuest_ = 0;
        if (found.empty()) {
            selectedQuest_ = -1;
            g_lastQuest.clear();
        } else {
            SortQuests(found.data(), found.data() + found.size());
            for (auto& s : found) {
                quests.push_back(s);
                const std::string& current = !listZones_ ? g_lastQuest : interface_->savedText_;
                if (text::CompareIgnoringCase(current, s) == 0) selectedQuest_ = static_cast<int>(quests.size()) - 1;
            }
            g_lastQuest = quests[selectedQuest_];
        }
        int rows = static_cast<int>(quests.size());
        bar_->SetMaximum(rows > 5 ? rows - 5 : 0);
        bar_->SetPosition(0);
        firstRow_ = 0;
        RefreshQuests();

        {
            std::string text; // (declared first: the title is released first)
            std::string title;
            bool foundText;
            {
                std::string zoneKey("zone ");
                std::string key = zoneKey + shownZone_;
                foundText = g_clientWorld.texts_->Open(key);
            }
            if (foundText) {
                g_clientWorld.texts_->ReadLine(&title, 1);
                g_clientWorld.texts_->ReadToHash(&text, 1);
                CUiSurface* surface = surfaces_[1];
                surface->Clear();
                RECT titleRect = {0, 5, 340, 25};
                surface->DrawTextIn(title, &titleRect, 1, 0x31B3FF, 1, 1);
                RECT textRect = {10, 25, 330, 140};
                surface->DrawTextIn(text, &textRect, 1, 0xA7D7E4, 0x11, 1);
            }
        }
    }
}

// --- The selected quest ------------------------------------------------------------------

void CObjectivesWindow::ShowQuest() {
    CTextManager* texts = g_clientWorld.texts_;
    std::string line;
    std::string part;
    std::string text;
    std::vector<std::string> fields;
    std::string variable;
    if (shownQuest_ == selectedQuest_) return;
    shownQuest_ = selectedQuest_;
    static_cast<CUiScrollText*>(children_[0])->Clear();
    std::vector<std::string>& quests = quests_;
    if (selectedQuest_ < 0 || selectedQuest_ >= static_cast<int>(quests.size())) return;
    g_lastQuest = quests[selectedQuest_];

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
        found = texts->Open(key);
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    texts->NextLine();
    texts->ReadToHash(&text, 1);
    {
        std::string colour("\007C\344\327\247 ");
        text = colour + text;
    }
    text += "\n ";
    if (QuestRecord* record = FindQuestRecord(quests[selectedQuest_])) {
        float experience = record->exp;
        text += "\007C\377\377\377 ";
        if (experience > 0.0f) {
            part = std::format("{} {}\n ", costExp, static_cast<int>(libcrt::Ftol(D(experience))));
            text += part;
        }
        float money = record->money;
        if (money > 0.0f) {
            part = std::format("{} {}\n ", costMoney, static_cast<int>(libcrt::Ftol(D(money))));
            text += part;
        }
    }
    while (texts->FindHeader()) {
        texts->Split(&fields, 1);
        if (fields.size() < 2) continue;
        if (text::CompareIgnoringCase(fields[0], "#quest") == 0) break;
        if (text::CompareIgnoringCase(fields[0], "#subobj") != 0) continue;
        variable = std::format("q.{}.{}.{}", shownZone_, quests[selectedQuest_],
                        fields[1]);
        int state = static_cast<int>(libcrt::Ftol(D(LocalPlayerVariable(variable))));
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
    texts->Close();
    static_cast<CUiScrollText*>(children_[0])->SetText(text);

    MapRecord* map = g_mapDatabase.FindMap(shownZone_);
    if (!map) return;
    LoadQuestPlaces(quests[selectedQuest_]);
    while (std::ssize(sceneObjects_) > std::ssize(exits_) + 0x15) RemoveSceneObjectAt(std::ssize(exits_) + 0x15);
    while (std::ssize(regions_) > std::ssize(exits_) + 9) RemoveRegionAt(std::ssize(exits_) + 9);
    const float red[3] = {1.0f, 0.0f, 0.0f};
    for (unsigned i = 0; i < static_cast<unsigned>(questPlaces_.end() - questPlaces_.begin()); ++i) {
        float worldWidth = static_cast<float>(static_cast<double>(map->sizeX << 5));
        float worldHeight = static_cast<float>(static_cast<double>(map->sizeY << 5));
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
        const UiPointF& place = questPlaces_[i];
        float x = static_cast<float>((D(place.x) - D(worldWidth) * 0.5f) * kPictureSpan / D(span) + kPictureCentreX);
        float y = static_cast<float>(kPictureCentreY - (D(place.y) - D(worldHeight) * 0.5f) * kPictureSpan / D(span));
        CUiMesh* marker = AddBlankSprite();
        float bottom = static_cast<float>(D(y) + kMarkerHalfWidth);
        float right = static_cast<float>(D(x) + kMarkerHalfWidth);
        float top = static_cast<float>(D(y) - kMarkerHalfWidth);
        float left = static_cast<float>(D(x) - kMarkerHalfWidth);
        marker->SetRectangle(5.0f, 100.0f, left, top, right, bottom, 0, 0, 0x80, 0x80);
        marker->texture_ = objects_[1];
        std::memcpy(marker->tint_, red, sizeof(red));
        CUiRegion* region = AddRegion();
        int bottomPixel = static_cast<int>(libcrt::Ftol(D(bottom)));
        int rightPixel = static_cast<int>(libcrt::Ftol(D(right)));
        int topPixel = static_cast<int>(libcrt::Ftol(D(top)));
        int leftPixel = static_cast<int>(libcrt::Ftol(D(left)));
        region->SetRect(leftPixel, topPixel, rightPixel, bottomPixel);
        region->SetTip(0x138F0);
    }
}

// --- Create ------------------------------------------------------------------------------

void CObjectivesWindow::CloseScreen() { render::g_renderState.alphaReference = savedAlphaRef_; }

void CObjectivesWindow::CreateScreen() {
    savedAlphaRef_ = render::g_renderState.alphaReference;
    render::g_renderState.alphaReference = 1;

    std::string text;
    std::string key;
    for (int k = 0; k < 3; ++k) {
        key = std::format("obj_{}", k);
        g_clientWorld.texts_->GetString(key, &text);
        objectiveTexts_.push_back(text);
    }

    // The text column.
    CUiScrollText* column = new CUiScrollText(interface_, game::kEmptyText, RECT{60, 190, 384, 510}, 10.0f);
    AddChild(column);
    bar_ = new CUiScrollbar(interface_, RECT{60, 30, 384, 150}, 3.0f, 0, 1.0f);
    AddChild(bar_);

    LoadTexture("saveload", TextureKind::Interface);
    LoadTexture("cross", TextureKind::Interface);
    AddSurface(NewSurface(interface_, 50, 20, 390, 160));
    AddSurface(NewSurface(interface_, 410, 380, 750, 520));
    AddSurface(NewSurface(interface_, 280, 540, 520, 580));

    if (twoButtons_)
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 163.0f, 536.0f, 237.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    else
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 363.0f, 536.0f, 437.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    CUiMesh* second = AddBlankSprite();
    second->SetRectangle(3.0f, 100.0f, 563.0f, 536.0f, 637.0f, 584.0f, 0xA0, 2, 0xEA, 0x32);
    if (!twoButtons_) second->visible_ = false;
    CUiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(3.0f, 100.0f, 60.0f, 30.0f, 370.0f, 54.0f, 0, 0, 0x100, 0x100);
    highlight->texture_ = nullptr;
    highlight->flatColour_ = 0xC8A06800;
    highlight->flags_ |= FigureMeshFlag::Flat;
    highlight->visible_ = false;
    AddBlankSprite();   // object 3: the zone picture, set up by the zone page
    CUiMesh* bottomShade = AddBlankSprite();
    bottomShade->SetRectangle(15.0f, 100.0f, 285.0f, 541.0f, 515.0f, 579.0f, 0, 0, 0x100, 0x100);
    bottomShade->texture_ = nullptr;
    bottomShade->flags_ |= FigureMeshFlag::Flat;
    bottomShade->flatColour_ = 0xA0000000;
    AddBlankSprite()->SetFrame(3.0f, 100.0f, 280.0f, 536.0f, 520.0f, 584.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    const float shades[4][4] = {{50.0f, 20.0f, 390.0f, 160.0f},
                                {50.0f, 180.0f, 390.0f, 520.0f},
                                {410.0f, 20.0f, 750.0f, 360.0f},
                                {410.0f, 380.0f, 750.0f, 520.0f}};
    for (const auto& s : shades) {
        CUiMesh* shade = AddBlankSprite();
        shade->SetRectangle(15.0f, 100.0f, s[0], s[1], s[2], s[3], 0, 0, 0x100, 0x100);
        shade->texture_ = nullptr;
        shade->flags_ |= FigureMeshFlag::Flat;
        shade->flatColour_ = 0xA0000000;
    }
    const float frames[4][4] = {{45.0f, 15.0f, 395.0f, 165.0f},
                                {45.0f, 175.0f, 395.0f, 525.0f},
                                {405.0f, 15.0f, 755.0f, 365.0f},
                                {405.0f, 375.0f, 755.0f, 525.0f}};
    for (const auto& f : frames)
        AddBlankSprite()->SetFrame(3.0f, 100.0f, f[0], f[1], f[2], f[3], 5.0f, 2, 0xFE, 0xB4, 0xF9);
    CUiMesh* left = AddBlankSprite();
    left->SetRectangle(3.0f, 100.0f, 414.0f, 384.0f, 443.0f, 404.0f, 2, 0x72, 0x1F, 0x86);
    left->visible_ = false;
    CUiMesh* right = AddBlankSprite();
    right->SetRectangle(3.0f, 100.0f, 717.0f, 384.0f, 746.0f, 404.0f, 0x21, 0x72, 0x3E, 0x86);
    right->visible_ = false;
    for (int top = 31; top < 151; top += 24) {
        CUiMesh* mark = AddBlankSprite();
        mark->SetRectangle(2.8f, 100.0f, 62.0f, static_cast<float>(top), 79.0f, static_cast<float>(top + 22), 4,
                           0xA4, 0x48, 0xFC);
        mark->visible_ = false;
        mark->texture_ = objects_[1];
    }

    // Regions 0-3: the buttons and the two arrows; 4-8: the list rows.
    if (twoButtons_)
        AddRect(this, 163, 536, 237, 584)->SetTip(0x138E4);
    else
        AddRect(this, 363, 536, 437, 584)->SetTip(0x138E6);
    AddRect(this, 563, 536, 637, 584)->SetTip(twoButtons_ ? 0x138E5 : 0);
    AddRect(this, 414, 384, 443, 404);
    AddRect(this, 717, 384, 746, 404);
    for (int top = 30; top < 150; top += 24) AddRect(this, 60, top, 360, top + 24)->SetTip(0x138EF);
    blinkTime_ = 0.0f;

    if (!listZones_) {
        ShowZone(zoneName_);
    } else {
        MapRecord* allodZone = g_mapDatabase.FindMap(zoneName_);
        if (!allodZone) {
            interface_->SetPendingValue(0);
            return;
        }
        // The zones of this allod in which the player holds quests ("q.<zone>.<quest>" = 1), and
        // the zone the interface's current quest belongs to.
        std::string current;
        std::vector<std::string> parts;
        NamedFloatTable table;
        GameLocalPlayerVariables("q.", &table);
        const std::string& interfaceQuest = interface_->savedText_;
        for (auto node = table.begin(); node != table.end(); ++node) {
            text::Split(node->first, &parts, '.');
            if (parts.size() != 3) continue;
            if (text::CompareIgnoringCase(interfaceQuest,
                                parts[2]) == 0)
                current = parts[1];
            std::uint32_t one;
            one = std::bit_cast<std::uint32_t>(node->second);
            if (one != 0x3F800000) continue;
            MapRecord* zone = g_mapDatabase.FindMap(parts[1]);
            if (!zone) continue;
            if (!IsMultiplayerClient() &&
                text::CompareIgnoringCase(zone->allod,
                                allodZone->allod) != 0)
                continue;
            bool listed = false;
            for (const std::string& z : zones_)
                if (str::Compare(zone->name, z) == 0) {
                    listed = true;
                    break;
                }
            if (!listed) zones_.push_back(zone->name);
        }
        if (zones_.empty() && !current.empty()) zones_.push_back(current);
        if (zones_.empty()) {
            interface_->SetPendingValue(0);
            return;
        }
        zoneIndex_ = 0;
        for (int i = 0; i < std::ssize(zones_); ++i)
            if (text::CompareIgnoringCase(current, zones_[i]) == 0) {
                zoneIndex_ = i;
                break;
            }
        if (std::ssize(zones_) > 1) {
            SceneMesh(14)->visible_ = true;
            SceneMesh(15)->visible_ = true;
            regions_[2]->SetTip(0x13948);
            regions_[3]->SetTip(0x13949);
        }
        ShowZone(zones_[zoneIndex_]);
    }
    interface_->OpenHelp(false);
}
void CObjectivesWindow::DrawQuestList() {
    CTextManager* texts = g_clientWorld.texts_;
    std::string title;
    std::string variable;
    auto mesh = [this](int index) { return SceneMesh(index); };
    mesh(2)->visible_ = false;
    auto* surface = surfaces_[0];
    surface->Clear();
    for (int i = 16; i < 21; ++i) mesh(i)->visible_ = false;
    std::vector<std::string>& quests = quests_;
    int row = firstRow_;
    int y = 13;
    for (int i = 16; i < 21 && row < static_cast<int>(quests.size()); ++i, ++row, y += 24) {
        CUiMesh* mark = mesh(i);
        mark->visible_ = true;
        bool found;
        {
            std::string questWord("quest ");
            std::string key = questWord + quests[row];
            found = texts->Open(key);
        }
        if (found) texts->ReadLine(&title, 1);
        else title = quests[row];
        variable = std::format("q.{}.{}", shownZone_, quests[row]);
        int state = static_cast<int>(LocalPlayerVariable(variable));
        if (state == 2) mark->textureOffsetU_ = 0.265625f;
        else if (state == 3) mark->textureOffsetU_ = 0.0f;
        else mark->visible_ = false;
        if (row == selectedQuest_) {
            mesh(2)->Place(215.0f, static_cast<float>(y + 29), 3.0f);
            mesh(2)->visible_ = true;
        }
        RECT rect{36, y, 320, y + 21};
        surface->DrawTextIn(title, &rect, 1, 0xA7D7E4, 0, 1);
    }
}

// --- What the window is opened for, and its help topic --------------------

void CObjectivesWindow::Setup(std::string_view zone, std::map<int, float>* exitDistances, bool twoButtons, bool listZones) {
    zoneName_ = zone;
    twoButtons_ = twoButtons;
    exitDistances_ = exitDistances;
    listZones_ = listZones;
}

const char* CObjectivesWindow::HelpTopic() { return twoButtons_ ? kGlobalMapTopic : kMissionTopic; }
