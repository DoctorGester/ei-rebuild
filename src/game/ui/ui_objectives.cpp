// The objectives window (ObjectivesWindow, ui_objectives.h): its Create, the zone page, the selected quest's description
// and places, its clicks and keys and the quest places. TAB in a zone opens it for the current zone; opened with
// `listZones` it lists the zones of the zone's allod in which the player has quests and shows one of them. See
// docs/notes/ui.md, section 8.
#include "game/ui/ui_objectives.h"
#include "game/console/console.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/app/key_bindings.h"
#include "engine/core/named_floats.h"
#include "game/map/map_database.h"
#include "game/players/player_client.h"
#include "game/world/quest_records.h"
#include "engine/data/text_manager.h"
#include "engine/core/text_split.h"
#include "game/players/player.h"
#include "game/ui/ui.h"
#include "lib/maths.h"
#include "game/ui/ui_float.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_scroll_text.h"
#include "game/ui/ui_scrollbar.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/intro_sort.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "render/render_state.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {
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

void Turn(UiMesh* sprite, float angle) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion q = Quaternion::AxisAngle(axis, angle);
    sprite->localRotation = q;
}

UiRegion* AddRect(UiWindow* window, int left, int top, int right, int bottom) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

// The exits' or the places' markers and regions released and forgotten.
void RemoveMarkers(UiWindow* window, std::vector<UiMesh*>* markers, std::vector<UiRegion*>* areas) {
    for (UiMesh* marker : *markers) window->RemoveSceneObject(marker);
    for (UiRegion* area : *areas) window->RemoveRegion(area);
    markers->clear();
    areas->clear();
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
constexpr std::uint32_t kObjectiveSearchStart = 0x5A0E1BCA;   // 1e16
constexpr float kExitTurn = /*007C8170*/ 1.5707964f;   // a static initialiser: pi * 0.5
constexpr float kExitTurnDown = 3.1415927f;

// The quest database's record of `name` (the index's operator[] adds a null entry for a new name).
QuestRecord* FindQuestRecord(std::string_view name) { return g_questDatabase.questsByName[std::string(name)]; }

const char* const kGlobalMapTopic = "quest_global_map";
const char* const kMissionTopic = "quest_mission";

constexpr const char* kOkSound = "buttons\\tutorial\\ok.wav";
constexpr const char* kSelectSound = "buttons\\objectives\\select.wav";
constexpr const char* kEnterSound = "buttons\\objectives\\enter.wav";

constexpr int kActionObjectives = 4;   // "obj" in the key table

constexpr float kNearestScale = 1.2f;
constexpr float kOtherExitU = -0.125f;   // 0xBE000000
// The blink: (cos(rate x t) x amplitude + base) x 255 (2.5, 0.3, 0.5;
// 5.0, 0.4, 0.6; 255).
constexpr float kSlowRate = 2.5f, kSlowAmplitude = 0.3f, kSlowBase = 0.5f;
constexpr float kFastRate = 5.0f, kFastAmplitude = 0.4f, kFastBase = 0.6f;
constexpr float kAlphaScale = 255.0f;
} // namespace

// --- The zone page ----------------------------------------------------------------------

void ObjectivesWindow::ShowZone(std::string_view zone) {
    shownZone = zone;
    while (std::ssize(objects) > 2) RemoveObjectAt(2);
    RemoveMarkers(this, &exitMarkers, &exitAreas);
    RemoveMarkers(this, &placeMarkers, &placeAreas);

    // The zone's picture (picture2), its size in map cells and its exits (the deploy rectangle x1, y1, x2, y2 and the
    // target zone, "none" when it leads nowhere).
    MapRecord* map = g_mapDatabase.FindMap(shownZone);
    if (!map) {
        g_interface.SetPendingValue(0);
        return;
    }

    LoadTexture(map->picture2, TextureKind::Interface);
    float width = static_cast<float>(map->sizeX);
    float height = static_cast<float>(map->sizeY);
    float larger = width < height ? height : width;
    float widthShare = width / larger;
    float heightShare = height / larger;
    float halfHeight = heightShare * kPictureHalf;
    float halfWidth = widthShare * kPictureHalf;
    int u1 = static_cast<int>(widthShare * kTexels);
    int v0 = static_cast<int>((1.0f - heightShare) * kTexels);
    zonePicture->SetRectangle(10.0f, kSpriteScale, kPictureCentreX - halfWidth, kPictureCentreY - halfHeight,
                              halfWidth + kPictureCentreX, halfHeight + kPictureCentreY, 0, v0, u1, 0x100);
    zonePicture->texture = objects[2];

    // The exits that lead somewhere: a marker (triangle pointing out of the picture) and a region.
    float nearest = FloatFromBits(kObjectiveSearchStart);
    nearestExit = -1;
    bool anyKnown = false;
    exits.clear();
    for (int i = 0; i < std::ssize(map->exits); ++i) {
        const ZoneExit& source = map->exits[i];
        if (text::CompareIgnoringCase(source.target,
                            "none") == 0)
            continue;
        int index = lib::Count(exits);
        ObjectiveExit* exit = &exits.emplace_back();
        exit->exit = i;
        exit->x = (source.deploy[2] + source.deploy[0]) * 0.5f;
        exit->y = (source.deploy[3] + source.deploy[1]) * 0.5f;
        exit->distance = 0.0f;
        exit->flag = false;
        if (exitDistances) {
            for (auto node = exitDistances->begin(); node != exitDistances->end(); ++node) {
                if (node->first == i) {
                    anyKnown = true;
                    exit->distance = node->second;
                    exit->flag = true;
                    if (node->second < nearest) {
                        nearestExit = index;
                        nearest = node->second;
                    }
                    break;
                }
            }
        } else {
            std::string name = std::format("z.{}", source.target);
            float value = LocalPlayerVariable(name);
            if (value != 1.0f) exit->flag = true;
        }

        float worldWidth = static_cast<float>(map->sizeX << 5);
        float worldHeight = static_cast<float>(map->sizeY << 5);
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
        exit = &exits[index];
        float dx = exit->x - worldWidth * 0.5f;
        float dy = exit->y - worldHeight * 0.5f;
        float sideX = dx / worldWidth;
        float sideY = dy / worldHeight;
        float mapX = dx * kPictureSpan / span + kPictureCentreX;
        float x = mapX;
        float y = kPictureCentreY - dy * kPictureSpan / span;
        exit->mapX = mapX;
        exit->mapY = y;

        UiMesh* marker = AddBlankSprite();
        exitMarkers.push_back(marker);
        float base = y + kMarkerHeight;
        marker->SetTriangle(5.0f, 100.0f, x, y, x - kMarkerHalfWidth, base, x + kMarkerHalfWidth,
                            base, 240.0f, 226.0f, 227.0f, 254.0f, 253.0f, 254.0f);
        markerScale = marker->scaleMatrix.m[0];
        Rect area;
        area = {static_cast<int>(x - kExitHalf), static_cast<int>(y - kExitHalf), static_cast<int>(x + kExitHalf), static_cast<int>(y + kExitHalf)};
        UiRegion* region = AddRegion();
        exitAreas.push_back(region);
        region->SetTip(0x138EE);
        // The marker points away from the picture's centre: sideways or up/down by the larger
        // offset, and the region moves 10 pixels the same way.
        if (AbsBits(sideY) >= AbsBits(sideX)) {
            if (sideY > 0.0f) {
                Turn(marker, kExitTurnDown);
                area.Offset(0, -10);
            } else {
                area.Offset(0, 10);
            }
        } else if (sideX > 0.0f) {
            Turn(marker, -kExitTurn);
            area.Offset(10, 0);
        } else {
            Turn(marker, kExitTurn);
            area.Offset(-10, 0);
        }
        region->SetRect(area);
    }
    SelectExit(nearestExit);
    if (twoButtons && !anyKnown) {
        okIcon->visible = false;
        okButton->SetTip(0);
    }

    std::vector<std::string>& currentQuests = quests;
    currentQuests.clear();
    {
        std::string prefix;
        std::vector<std::string> parts;
        NamedFloatTable table;
        prefix = std::format("q.{}", shownZone);
        g_clientWorld.LocalView()->player->GetParty()->variables.CopyPrefixed(prefix, &table);
        std::vector<std::string> found;
        for (auto node = table.begin(); node != table.end(); ++node) {
            if (node->second == 0.0f) continue;
            parts = text::Split(node->first, '.');
            if (parts.size() == 3) found.push_back(parts[2]);
        }
        shownQuest = -1;
        selectedQuest = 0;
        if (found.empty()) {
            selectedQuest = -1;
            g_lastQuest.clear();
        } else {
            SortQuests(found.data(), found.data() + found.size());
            for (auto& s : found) {
                currentQuests.push_back(s);
                const std::string& current = !listZones ? g_lastQuest : g_interface.savedText;
                if (text::CompareIgnoringCase(current, s) == 0) selectedQuest = static_cast<int>(currentQuests.size()) - 1;
            }
            g_lastQuest = currentQuests[selectedQuest];
        }
        int count = static_cast<int>(currentQuests.size());
        bar->SetMaximum(count > kVisibleRows ? count - kVisibleRows : 0);
        bar->SetPosition(0);
        firstRow = 0;
        RefreshQuests();

        {
            std::string text; // (declared first: the title is released first)
            std::string title;
            bool foundText;
            {
                std::string zoneKey("zone ");
                std::string key = zoneKey + shownZone;
                foundText = g_clientWorld.texts->Open(key);
            }
            if (foundText) {
                title = g_clientWorld.texts->ReadLine(true);
                text = g_clientWorld.texts->ReadToHash();
                zoneTitle = title;
                zoneText = text;
            }
        }
    }
}

// --- The selected quest ------------------------------------------------------------------

void ObjectivesWindow::ShowQuest() {
    TextManager* texts = g_clientWorld.texts.get();
    std::string line;
    std::string part;
    std::string text;
    std::vector<std::string> fields;
    std::string variable;
    if (shownQuest == selectedQuest) return;
    shownQuest = selectedQuest;
    questText->Clear();
    std::vector<std::string>& currentQuests = quests;
    if (selectedQuest < 0 || selectedQuest >= static_cast<int>(currentQuests.size())) return;
    g_lastQuest = currentQuests[selectedQuest];

    std::string completed;
    std::string failed;
    std::string costExp;
    std::string costMoney;
    completed = texts->GetString("obj_completed");
    failed = texts->GetString("obj_failed");
    costExp = texts->GetString("obj_cost_exp");
    costMoney = texts->GetString("obj_cost_money");
    bool found;
    {
        std::string questWord("quest ");
        std::string key = questWord + currentQuests[selectedQuest];
        found = texts->Open(key);
    }
    if (!found) return;

    // The text: a colour code, the quest's description, its rewards, then each sub-objective the
    // player has (q.<zone>.<quest>.<objective> not 0) with "completed" (2) or "failed" (3).
    texts->NextLine();
    text = texts->ReadToHash();
    {
        std::string colour("\007C\344\327\247 ");
        text = colour + text;
    }
    text += "\n ";
    if (QuestRecord* record = FindQuestRecord(currentQuests[selectedQuest])) {
        float experience = record->exp;
        text += "\007C\377\377\377 ";
        if (experience > 0.0f) {
            part = std::format("{} {}\n ", costExp, static_cast<int>(experience));
            text += part;
        }
        float money = record->money;
        if (money > 0.0f) {
            part = std::format("{} {}\n ", costMoney, static_cast<int>(money));
            text += part;
        }
    }
    while (texts->FindHeader()) {
        fields = texts->Split(true);
        if (fields.size() < 2) continue;
        if (text::CompareIgnoringCase(fields[0], "#quest") == 0) break;
        if (text::CompareIgnoringCase(fields[0], "#subobj") != 0) continue;
        variable = std::format("q.{}.{}.{}", shownZone, currentQuests[selectedQuest],
                        fields[1]);
        int state = static_cast<int>(LocalPlayerVariable(variable));
        if (state == 0) continue;
        line = texts->ReadLine(true);
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
        line = texts->ReadToHash();
        text += line;
        text += "\n \n ";
    }
    texts->Close();
    questText->SetText(text);

    MapRecord* map = g_mapDatabase.FindMap(shownZone);
    if (!map) return;
    LoadQuestPlaces(currentQuests[selectedQuest]);
    RemoveMarkers(this, &placeMarkers, &placeAreas);
    const float red[3] = {1.0f, 0.0f, 0.0f};
    for (unsigned i = 0; i < static_cast<unsigned>(questPlaces.end() - questPlaces.begin()); ++i) {
        float worldWidth = static_cast<float>(map->sizeX << 5);
        float worldHeight = static_cast<float>(map->sizeY << 5);
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
        const Vector2& place = questPlaces[i];
        float x = (place.x - worldWidth * 0.5f) * kPictureSpan / span + kPictureCentreX;
        float y = kPictureCentreY - (place.y - worldHeight * 0.5f) * kPictureSpan / span;
        UiMesh* marker = AddBlankSprite();
        placeMarkers.push_back(marker);
        float bottom = y + kMarkerHalfWidth;
        float right = x + kMarkerHalfWidth;
        float top = y - kMarkerHalfWidth;
        float left = x - kMarkerHalfWidth;
        marker->SetRectangle(5.0f, kSpriteScale, left, top, right, bottom, 0, 0, 0x80, 0x80);
        marker->texture = objects[1];
        std::memcpy(marker->tint, red, sizeof(red));
        UiRegion* region = AddRegion();
        placeAreas.push_back(region);
        int bottomPixel = static_cast<int>(bottom);
        int rightPixel = static_cast<int>(right);
        int topPixel = static_cast<int>(top);
        int leftPixel = static_cast<int>(left);
        region->SetRect(leftPixel, topPixel, rightPixel, bottomPixel);
        region->SetTip(0x138F0);
    }
}

// --- Create ------------------------------------------------------------------------------

void ObjectivesWindow::CloseScreen() { render::g_renderState.alphaReference = savedAlphaRef; }

void ObjectivesWindow::CreateScreen() {
    savedAlphaRef = render::g_renderState.alphaReference;
    render::g_renderState.alphaReference = 1;

    std::string text;
    std::string key;
    for (int k = 0; k < 3; ++k) {
        key = std::format("obj_{}", k);
        text = g_clientWorld.texts->GetString(key);
        objectiveTexts.push_back(text);
    }

    // The text column.
    questText = AddChild(std::make_unique<UiScrollText>("", Rect{60, 190, 384, 510}, 10.0f));
    bar = AddChild(std::make_unique<UiScrollbar>(Rect{60, 30, 384, 150}, 3.0f, 0, 1.0f));

    LoadTexture("saveload", TextureKind::Interface);
    LoadTexture("cross", TextureKind::Interface);
    AddSurface(NewSurface(Rect{50, 20, 390, 160}))->PaintWith([this](UiSurface& out) { PaintQuestList(out); });
    AddSurface(NewSurface(Rect{410, 380, 750, 520}))->PaintWith([this](UiSurface& out) { PaintZone(out); });
    AddSurface(NewSurface(Rect{280, 540, 520, 580}));

    okIcon = AddBlankSprite();
    if (twoButtons)
        okIcon->SetRectangle(kDialogDepth, kSpriteScale, 163.0f, 536.0f, 237.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    else
        okIcon->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, 536.0f, 437.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    UiMesh* second = AddBlankSprite();
    second->SetRectangle(kDialogDepth, kSpriteScale, 563.0f, 536.0f, 637.0f, 584.0f, 0xA0, 2, 0xEA, 0x32);
    if (!twoButtons) second->visible = false;
    selectionMarker = AddBlankSprite();
    selectionMarker->SetRectangle(kDialogDepth, kSpriteScale, 60.0f, 30.0f, 370.0f, 54.0f, 0, 0, kUiTextureSize,
                                  kUiTextureSize);
    selectionMarker->texture = nullptr;
    selectionMarker->flatColour = 0xC8A06800;
    selectionMarker->flags |= FigureMeshFlag::Flat;
    selectionMarker->visible = false;
    zonePicture = AddBlankSprite();   // set up by the zone page
    bottomShade = AddBlankSprite();
    bottomShade->SetRectangle(15.0f, kSpriteScale, 285.0f, 541.0f, 515.0f, 579.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    bottomShade->texture = nullptr;
    bottomShade->flags |= FigureMeshFlag::Flat;
    bottomShade->flatColour = 0xA0000000;
    bottomFrame = AddBlankSprite();
    bottomFrame->SetFrame(3.0f, 100.0f, 280.0f, 536.0f, 520.0f, 584.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    const float shades[4][4] = {{50.0f, 20.0f, 390.0f, 160.0f},
                                {50.0f, 180.0f, 390.0f, 520.0f},
                                {410.0f, 20.0f, 750.0f, 360.0f},
                                {410.0f, 380.0f, 750.0f, 520.0f}};
    for (const auto& s : shades) {
        UiMesh* shade = AddBlankSprite();
        shade->SetRectangle(15.0f, kSpriteScale, s[0], s[1], s[2], s[3], 0, 0, kUiTextureSize, kUiTextureSize);
        shade->texture = nullptr;
        shade->flags |= FigureMeshFlag::Flat;
        shade->flatColour = 0xA0000000;
    }
    const float frames[4][4] = {{45.0f, 15.0f, 395.0f, 165.0f},
                                {45.0f, 175.0f, 395.0f, 525.0f},
                                {405.0f, 15.0f, 755.0f, 365.0f},
                                {405.0f, 375.0f, 755.0f, 525.0f}};
    for (const auto& f : frames)
        AddBlankSprite()->SetFrame(3.0f, 100.0f, f[0], f[1], f[2], f[3], 5.0f, 2, 0xFE, 0xB4, 0xF9);
    previousZoneArrow = AddBlankSprite();
    previousZoneArrow->SetRectangle(kDialogDepth, kSpriteScale, 414.0f, 384.0f, 443.0f, 404.0f, 2, 0x72, 0x1F, 0x86);
    previousZoneArrow->visible = false;
    nextZoneArrow = AddBlankSprite();
    nextZoneArrow->SetRectangle(kDialogDepth, kSpriteScale, 717.0f, 384.0f, 746.0f, 404.0f, 0x21, 0x72, 0x3E, 0x86);
    nextZoneArrow->visible = false;
    for (int row = 0, top = 31; row < kVisibleRows; ++row, top += 24) {
        UiMesh* mark = AddBlankSprite();
        questMarks[row] = mark;
        mark->SetRectangle(2.8f, kSpriteScale, 62.0f, static_cast<float>(top), 79.0f, static_cast<float>(top + 22), 4,
                           0xA4, 0x48, 0xFC);
        mark->visible = false;
        mark->texture = objects[1];
    }

    if (twoButtons) {
        okButton = AddRect(this, 163, 536, 237, 584);
        okButton->SetTip(0x138E4);
    } else {
        okButton = AddRect(this, 363, 536, 437, 584);
        okButton->SetTip(0x138E6);
    }
    cancelButton = AddRect(this, 563, 536, 637, 584);
    cancelButton->SetTip(twoButtons ? 0x138E5 : 0);
    previousZoneButton = AddRect(this, 414, 384, 443, 404);
    nextZoneButton = AddRect(this, 717, 384, 746, 404);
    for (int row = 0, top = 30; row < kVisibleRows; ++row, top += 24) {
        rows[row] = AddRect(this, 60, top, 360, top + 24);
        rows[row]->SetTip(0x138EF);
    }
    blinkTime = 0.0f;

    if (!listZones) {
        ShowZone(zoneName);
    } else {
        MapRecord* allodZone = g_mapDatabase.FindMap(zoneName);
        if (!allodZone) {
            g_interface.SetPendingValue(0);
            return;
        }
        // The zones of this allod in which the player holds quests ("q.<zone>.<quest>" = 1), and
        // the zone the interface's current quest belongs to.
        std::string current;
        std::vector<std::string> parts;
        NamedFloatTable table;
        g_clientWorld.LocalView()->player->GetParty()->variables.CopyPrefixed("q.", &table);
        const std::string& interfaceQuest = g_interface.savedText;
        for (auto node = table.begin(); node != table.end(); ++node) {
            parts = text::Split(node->first, '.');
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
            for (const std::string& z : zones)
                if (str::Compare(zone->name, z) == 0) {
                    listed = true;
                    break;
                }
            if (!listed) zones.push_back(zone->name);
        }
        if (zones.empty() && !current.empty()) zones.push_back(current);
        if (zones.empty()) {
            g_interface.SetPendingValue(0);
            return;
        }
        zoneIndex = 0;
        for (int i = 0; i < std::ssize(zones); ++i)
            if (text::CompareIgnoringCase(current, zones[i]) == 0) {
                zoneIndex = i;
                break;
            }
        if (std::ssize(zones) > 1) {
            previousZoneArrow->visible = true;
            nextZoneArrow->visible = true;
            previousZoneButton->SetTip(0x13948);
            nextZoneButton->SetTip(0x13949);
        }
        ShowZone(zones[zoneIndex]);
    }
    g_interface.OpenHelp(false);
}

void ObjectivesWindow::DrawQuestList() {
    TextManager* texts = g_clientWorld.texts.get();
    std::string title;
    std::string variable;
    selectionMarker->visible = false;
    questTitles.clear();
    for (UiMesh* mark : questMarks) mark->visible = false;
    std::vector<std::string>& currentQuests = quests;
    int row = firstRow;
    int y = 13;
    for (int i = 0; i < kVisibleRows && row < static_cast<int>(currentQuests.size()); ++i, ++row, y += 24) {
        UiMesh* mark = questMarks[i];
        mark->visible = true;
        bool found;
        {
            std::string questWord("quest ");
            std::string key = questWord + currentQuests[row];
            found = texts->Open(key);
        }
        if (found) title = texts->ReadLine(true);
        else title = currentQuests[row];
        variable = std::format("q.{}.{}", shownZone, currentQuests[row]);
        int state = static_cast<int>(LocalPlayerVariable(variable));
        if (state == 2) mark->textureOffsetU = 0.265625f;
        else if (state == 3) mark->textureOffsetU = 0.0f;
        else mark->visible = false;
        if (row == selectedQuest) {
            selectionMarker->Place(215.0f, static_cast<float>(y + 29), 3.0f);
            selectionMarker->visible = true;
        }
        questTitles.push_back(title);
    }
}

void ObjectivesWindow::PaintQuestList(UiSurface& surface) const {
    int y = 13;
    for (const std::string& title : questTitles) {
        Rect area{36, y, 320, y + 21};
        surface.DrawTextIn(title, area, TextStyle{1, kUiTextColour});
        y += 24;
    }
}

void ObjectivesWindow::PaintZone(UiSurface& surface) const {
    Rect titleRect = {0, 5, 340, 25};
    surface.DrawTextIn(zoneTitle, titleRect, TextStyle{1, 0x31B3FF, render::text::Format::Center});
    Rect textRect = {10, 25, 330, 140};
    surface.DrawTextIn(zoneText, textRect, TextStyle{1, kUiTextColour, render::text::Format::Center | render::text::Format::WordBreak});
}

// --- What the window is opened for, and its help topic --------------------

void ObjectivesWindow::Setup(std::string_view zone, std::map<int, float>* newExitDistances, bool newTwoButtons, bool newListZones) {
    zoneName = zone;
    twoButtons = newTwoButtons;
    exitDistances = newExitDistances;
    listZones = newListZones;
}

const char* ObjectivesWindow::HelpTopic() { return twoButtons ? kGlobalMapTopic : kMissionTopic; }

void ObjectivesWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void ObjectivesWindow::LeftDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    UiRegion* region = RegionUnder(event.at);
    std::uint32_t zoneCount = static_cast<std::uint32_t>(zones.end() - zones.begin());
    if (region == okButton) {
        g_interface.PlayInterfaceSound(kOkSound);
        if (!twoButtons) {
            g_interface.SetPendingValue(0);
        } else if (nearestExit >= 0) {
            const ObjectiveExit& exit = exits[nearestExit];
            int index = exit.exit;
            g_serverWorld.PassTime(exit.distance);
            RequestZone(shownZone, index);
        }
        return;
    }
    if (region == cancelButton) {
        if (twoButtons) {
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            g_interface.SetPendingValue(0);
        }
        return;
    }
    if (region == previousZoneButton) {
        if (!listZones || zoneCount <= 1) return;
        if (--zoneIndex < 0) zoneIndex = static_cast<int>(zoneCount) - 1;
        ShowZone(zones[zoneIndex]);
        return;
    }
    if (region == nextZoneButton) {
        if (!listZones || zoneCount <= 1) return;
        if (static_cast<std::uint32_t>(++zoneIndex) >= zoneCount) zoneIndex = 0;
        ShowZone(zones[zoneIndex]);
        return;
    }
    if (const int shownRow = RegionIndex(rows, region); shownRow >= 0) {
        int row = firstRow + shownRow;
        if (row >= 0 && row < static_cast<int>(quests.size())) {
            g_interface.PlayInterfaceSound(kSelectSound);
            selectedQuest = row;
            DrawQuestList();
            ShowQuest();
        }
        return;
    }
    const int exit = RegionIndex(exitAreas, region);
    if (exit < 0) return;
    if (twoButtons && exits[exit].flag) {
        g_interface.PlayInterfaceSound(kEnterSound);
        SelectExit(exit);
        return;
    }
    g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
}

void ObjectivesWindow::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    int step = 0;
    switch (event.key) {
    case platform::Key::Return:
        g_interface.PlayInterfaceSound(kOkSound);
        if (!twoButtons) {
            g_interface.SetPendingValue(0);
        } else if (nearestExit >= 0) {
            const ObjectiveExit& exit = exits[nearestExit];
            int index = exit.exit;
            g_serverWorld.PassTime(exit.distance);
            RequestZone(shownZone, index);
        }
        event.handled = true;
        break;
    case platform::Key::Escape:
        g_interface.PlayInterfaceSound(!twoButtons ? kOkSound : kMessageBoxCancelSound);
        g_interface.SetPendingValue(0);
        event.handled = true;
        break;
    case platform::Key::PageUp: step = -5; break;
    case platform::Key::PageDown: step = 5; break;
    case platform::Key::Up: step = -1; break;
    case platform::Key::Down: step = 1; break;
    default: break;
    }
    if (step != 0) {
        g_interface.PlayInterfaceSound(kSelectSound);
        selectedQuest += step;
        RefreshQuests();
        event.handled = true;
    }
    if (!event.handled && g_keyActions[event.scan] == kActionObjectives && !twoButtons) {
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        g_interface.SetPendingValue(0);
    }
}

void ObjectivesWindow::LoadQuestPlaces(std::string_view name) {
    std::vector<Vector2>& places = questPlaces;
    places.clear();
    if (IsMultiplayerClient()) {
        // (The index's operator[] adds a null entry for a new name.)
        QuestRecord* record = g_questDatabase.questsByName[std::string(name)];
        if (record == nullptr) return;
        for (std::uint32_t i = 1; i < record->places.size(); i += 2)
            places.push_back(Vector2{record->places[i - 1], record->places[i]});
        return;
    }
    if (const MapQuestRecord* quest = g_mapDatabase.FindQuest(name)) places = quest->places;
}

// --- Quest list handlers --------------------------------------------------------

void ObjectivesWindow::SelectExit(int index) {
    nearestExit = index;
    for (int i = 0; i < std::ssize(exits); ++i) {
        UiMesh* marker = exitMarkers[i];
        if (i == index) {
            marker->SetScale(markerScale * kNearestScale);
            marker->textureOffsetU = 0.0f;
            blinkTime = 0.0f;
        } else {
            marker->SetScale(markerScale);
            marker->textureOffsetU = exits[i].flag ? 0.0f : kOtherExitU;
        }
    }
    bottomShade->visible = false;
    bottomFrame->visible = false;
}

void ObjectivesWindow::RefreshQuests() {
    const auto& currentQuests = quests;
    if (static_cast<int>(currentQuests.size()) == 0) return;
    if (selectedQuest < 0) selectedQuest = 0;
    if (selectedQuest >= static_cast<int>(currentQuests.size())) selectedQuest = static_cast<int>(currentQuests.size()) - 1;
    const int row = selectedQuest;
    if (firstRow + kVisibleRows <= row) firstRow = row - (kVisibleRows - 1);
    if (row < firstRow) firstRow = row;
    bar->SetPosition(firstRow);
    firstRow = bar->TakePosition();
    DrawQuestList();
    ShowQuest();
}

void ObjectivesWindow::Update() {
    UiContainer::Update();
    if (bar->TakeMoved()) {
        firstRow = bar->Position();
        DrawQuestList();
    }
    blinkTime += FrameTime();
    const std::uint32_t slow =
        static_cast<std::uint32_t>((lib::Cos(blinkTime * kSlowRate) * kSlowAmplitude + kSlowBase) * kAlphaScale);
    const std::uint32_t fast =
        static_cast<std::uint32_t>((lib::Cos(blinkTime * kFastRate) * kFastAmplitude + kFastBase) * kAlphaScale);
    for (UiMesh* place : placeMarkers) {
        place->flags |= FigureMeshFlag::Flat;
        place->flatColour = fast << 24 | 0xFF0000u;
    }
    for (int i = 0; i < std::ssize(exitMarkers); ++i) {
        UiMesh* marker = exitMarkers[i];
        marker->flags |= FigureMeshFlag::Flat;
        marker->flatColour = i == nearestExit ? fast << 24 | 0xFFFFFFu : slow << 24 | 0xC8C8C8u;
    }
}
