// The objectives window (ObjectivesWindow): its Create, the zone page and the selected quest's description
// and places. TAB in a zone opens it for the current zone; opened with `listZones` it lists the zones of the
// zone's allod in which the player has quests and shows one of them. See docs/notes/ui.md.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "text_manager.h"
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


void Turn(UiMesh* sprite, float angle) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    sprite->localRotation = q;
}

UiRegion* AddRect(UiWindow* window, int left, int top, int right, int bottom) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

UiSurface* NewSurface(Interface3D* ui, int left, int top, int right, int bottom) {
    return new UiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
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

void ObjectivesWindow::ShowZone(std::string_view zone) {
    shownZone = zone;
    while (std::ssize(objects) > 2) RemoveObjectAt(2);
    while (std::ssize(sceneObjects) > 0x15) RemoveSceneObjectAt(0x15);
    while (std::ssize(regions) > 9) RemoveRegionAt(9);

    // The zone's picture (picture2), its size in map cells and its exits (the deploy rectangle x1, y1, x2, y2 and the
    // target zone, "none" when it leads nowhere).
    MapRecord* map = g_mapDatabase.FindMap(shownZone);
    if (!map) {
        ui->SetPendingValue(0);
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
    int u1 = static_cast<int>(libcrt::Ftol(widthShare * kTexels));
    int v0 = static_cast<int>(libcrt::Ftol((1.0f - D(heightShare)) * kTexels));
    UiMesh* picture = SceneMesh(3);
    picture->SetRectangle(10.0f, 100.0f, kPictureCentreX - halfWidth, kPictureCentreY - halfHeight,
                          halfWidth + kPictureCentreX, halfHeight + kPictureCentreY, 0, v0, u1,
                          0x100);
    picture->texture = objects[2];

    // The exits that lead somewhere: a marker (triangle pointing out of the picture) and a region.
    float nearest = FloatFromBits(kFarAway);
    nearestExit = -1;
    bool anyKnown = false;
    exits.clear();
    for (int i = 0; i < std::ssize(map->exits); ++i) {
        const ZoneExit& source = map->exits[i];
        if (text::CompareIgnoringCase(source.target,
                            "none") == 0)
            continue;
        int index = std::ssize(exits);
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
                    EI_NAN_AUDIT(node->second, nearest);
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
            EI_NAN_AUDIT(value);
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
        float base = y + kMarkerHeight;
        marker->SetTriangle(5.0f, 100.0f, x, y, x - kMarkerHalfWidth, base, x + kMarkerHalfWidth,
                            base, 240.0f, 226.0f, 227.0f, 254.0f, 253.0f, 254.0f);
        markerScale = marker->scaleMatrix.m[0];
        RECT area;
        area.left = static_cast<int>(libcrt::Ftol(D(x) - kExitHalf));
        area.top = static_cast<int>(libcrt::Ftol(D(y) - kExitHalf));
        area.right = static_cast<int>(libcrt::Ftol(D(x) + kExitHalf));
        area.bottom = static_cast<int>(libcrt::Ftol(D(y) + kExitHalf));
        UiRegion* region = AddRegion();
        region->SetTip(0x138EE);
        // The marker points away from the picture's centre: sideways or up/down by the larger
        // offset, and the region moves 10 pixels the same way.
        if (AbsBits(sideY) >= AbsBits(sideX)) {
            if (sideY > 0.0f) {
                Turn(marker, kExitTurnDown);
                gameapi::OffsetRect(&area, 0, -10);
            } else {
                gameapi::OffsetRect(&area, 0, 10);
            }
        } else if (sideX > 0.0f) {
            Turn(marker, -kExitTurn);
            gameapi::OffsetRect(&area, 10, 0);
        } else {
            Turn(marker, kExitTurn);
            gameapi::OffsetRect(&area, -10, 0);
        }
        region->SetRect(area.left, area.top, area.right, area.bottom);
    }
    SelectExit(nearestExit);
    if (twoButtons && !anyKnown) {
        SceneMesh(0)->visible = false;
        regions[0]->SetTip(0);
    }

    std::vector<std::string>& currentQuests = quests;
    currentQuests.clear();
    {
        std::string prefix;
        std::vector<std::string> parts;
        NamedFloatTable table;
        prefix = std::format("q.{}", shownZone);
        GameLocalPlayerVariables(prefix, &table);
        std::vector<std::string> found;
        for (auto node = table.begin(); node != table.end(); ++node) {
            EI_NAN_AUDIT(node->second);
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
                const std::string& current = !listZones ? g_lastQuest : ui->savedText;
                if (text::CompareIgnoringCase(current, s) == 0) selectedQuest = static_cast<int>(currentQuests.size()) - 1;
            }
            g_lastQuest = currentQuests[selectedQuest];
        }
        int rows = static_cast<int>(currentQuests.size());
        bar->SetMaximum(rows > 5 ? rows - 5 : 0);
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
                UiSurface* surface = surfaces[1];
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

void ObjectivesWindow::ShowQuest() {
    TextManager* texts = g_clientWorld.texts;
    std::string line;
    std::string part;
    std::string text;
    std::vector<std::string> fields;
    std::string variable;
    if (shownQuest == selectedQuest) return;
    shownQuest = selectedQuest;
    static_cast<UiScrollText*>(children[0])->Clear();
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
        fields = texts->Split(true);
        if (fields.size() < 2) continue;
        if (text::CompareIgnoringCase(fields[0], "#quest") == 0) break;
        if (text::CompareIgnoringCase(fields[0], "#subobj") != 0) continue;
        variable = std::format("q.{}.{}.{}", shownZone, currentQuests[selectedQuest],
                        fields[1]);
        int state = static_cast<int>(libcrt::Ftol(D(LocalPlayerVariable(variable))));
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
    static_cast<UiScrollText*>(children[0])->SetText(text);

    MapRecord* map = g_mapDatabase.FindMap(shownZone);
    if (!map) return;
    LoadQuestPlaces(currentQuests[selectedQuest]);
    while (std::ssize(sceneObjects) > std::ssize(exits) + 0x15) RemoveSceneObjectAt(std::ssize(exits) + 0x15);
    while (std::ssize(regions) > std::ssize(exits) + 9) RemoveRegionAt(std::ssize(exits) + 9);
    const float red[3] = {1.0f, 0.0f, 0.0f};
    for (unsigned i = 0; i < static_cast<unsigned>(questPlaces.end() - questPlaces.begin()); ++i) {
        float worldWidth = static_cast<float>(map->sizeX << 5);
        float worldHeight = static_cast<float>(map->sizeY << 5);
        float span = worldWidth < worldHeight ? worldHeight : worldWidth;
        const UiPointF& place = questPlaces[i];
        float x = (place.x - worldWidth * 0.5f) * kPictureSpan / span + kPictureCentreX;
        float y = kPictureCentreY - (place.y - worldHeight * 0.5f) * kPictureSpan / span;
        UiMesh* marker = AddBlankSprite();
        float bottom = y + kMarkerHalfWidth;
        float right = x + kMarkerHalfWidth;
        float top = y - kMarkerHalfWidth;
        float left = x - kMarkerHalfWidth;
        marker->SetRectangle(5.0f, 100.0f, left, top, right, bottom, 0, 0, 0x80, 0x80);
        marker->texture = objects[1];
        std::memcpy(marker->tint, red, sizeof(red));
        UiRegion* region = AddRegion();
        int bottomPixel = static_cast<int>(libcrt::Ftol(D(bottom)));
        int rightPixel = static_cast<int>(libcrt::Ftol(D(right)));
        int topPixel = static_cast<int>(libcrt::Ftol(D(top)));
        int leftPixel = static_cast<int>(libcrt::Ftol(D(left)));
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
    UiScrollText* column = new UiScrollText(ui, game::kEmptyText, RECT{60, 190, 384, 510}, 10.0f);
    AddChild(column);
    bar = new UiScrollbar(ui, RECT{60, 30, 384, 150}, 3.0f, 0, 1.0f);
    AddChild(bar);

    LoadTexture("saveload", TextureKind::Interface);
    LoadTexture("cross", TextureKind::Interface);
    AddSurface(NewSurface(ui, 50, 20, 390, 160));
    AddSurface(NewSurface(ui, 410, 380, 750, 520));
    AddSurface(NewSurface(ui, 280, 540, 520, 580));

    if (twoButtons)
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 163.0f, 536.0f, 237.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    else
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 363.0f, 536.0f, 437.0f, 584.0f, 0x51, 2, 0x9B, 0x32);
    UiMesh* second = AddBlankSprite();
    second->SetRectangle(3.0f, 100.0f, 563.0f, 536.0f, 637.0f, 584.0f, 0xA0, 2, 0xEA, 0x32);
    if (!twoButtons) second->visible = false;
    UiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(3.0f, 100.0f, 60.0f, 30.0f, 370.0f, 54.0f, 0, 0, 0x100, 0x100);
    highlight->texture = nullptr;
    highlight->flatColour = 0xC8A06800;
    highlight->flags |= FigureMeshFlag::Flat;
    highlight->visible = false;
    AddBlankSprite();   // object 3: the zone picture, set up by the zone page
    UiMesh* bottomShade = AddBlankSprite();
    bottomShade->SetRectangle(15.0f, 100.0f, 285.0f, 541.0f, 515.0f, 579.0f, 0, 0, 0x100, 0x100);
    bottomShade->texture = nullptr;
    bottomShade->flags |= FigureMeshFlag::Flat;
    bottomShade->flatColour = 0xA0000000;
    AddBlankSprite()->SetFrame(3.0f, 100.0f, 280.0f, 536.0f, 520.0f, 584.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    const float shades[4][4] = {{50.0f, 20.0f, 390.0f, 160.0f},
                                {50.0f, 180.0f, 390.0f, 520.0f},
                                {410.0f, 20.0f, 750.0f, 360.0f},
                                {410.0f, 380.0f, 750.0f, 520.0f}};
    for (const auto& s : shades) {
        UiMesh* shade = AddBlankSprite();
        shade->SetRectangle(15.0f, 100.0f, s[0], s[1], s[2], s[3], 0, 0, 0x100, 0x100);
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
    UiMesh* left = AddBlankSprite();
    left->SetRectangle(3.0f, 100.0f, 414.0f, 384.0f, 443.0f, 404.0f, 2, 0x72, 0x1F, 0x86);
    left->visible = false;
    UiMesh* right = AddBlankSprite();
    right->SetRectangle(3.0f, 100.0f, 717.0f, 384.0f, 746.0f, 404.0f, 0x21, 0x72, 0x3E, 0x86);
    right->visible = false;
    for (int top = 31; top < 151; top += 24) {
        UiMesh* mark = AddBlankSprite();
        mark->SetRectangle(2.8f, 100.0f, 62.0f, static_cast<float>(top), 79.0f, static_cast<float>(top + 22), 4,
                           0xA4, 0x48, 0xFC);
        mark->visible = false;
        mark->texture = objects[1];
    }

    // Regions 0-3: the buttons and the two arrows; 4-8: the list rows.
    if (twoButtons)
        AddRect(this, 163, 536, 237, 584)->SetTip(0x138E4);
    else
        AddRect(this, 363, 536, 437, 584)->SetTip(0x138E6);
    AddRect(this, 563, 536, 637, 584)->SetTip(twoButtons ? 0x138E5 : 0);
    AddRect(this, 414, 384, 443, 404);
    AddRect(this, 717, 384, 746, 404);
    for (int top = 30; top < 150; top += 24) AddRect(this, 60, top, 360, top + 24)->SetTip(0x138EF);
    blinkTime = 0.0f;

    if (!listZones) {
        ShowZone(zoneName);
    } else {
        MapRecord* allodZone = g_mapDatabase.FindMap(zoneName);
        if (!allodZone) {
            ui->SetPendingValue(0);
            return;
        }
        // The zones of this allod in which the player holds quests ("q.<zone>.<quest>" = 1), and
        // the zone the interface's current quest belongs to.
        std::string current;
        std::vector<std::string> parts;
        NamedFloatTable table;
        GameLocalPlayerVariables("q.", &table);
        const std::string& interfaceQuest = ui->savedText;
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
            ui->SetPendingValue(0);
            return;
        }
        zoneIndex = 0;
        for (int i = 0; i < std::ssize(zones); ++i)
            if (text::CompareIgnoringCase(current, zones[i]) == 0) {
                zoneIndex = i;
                break;
            }
        if (std::ssize(zones) > 1) {
            SceneMesh(14)->visible = true;
            SceneMesh(15)->visible = true;
            regions[2]->SetTip(0x13948);
            regions[3]->SetTip(0x13949);
        }
        ShowZone(zones[zoneIndex]);
    }
    ui->OpenHelp(false);
}
void ObjectivesWindow::DrawQuestList() {
    TextManager* texts = g_clientWorld.texts;
    std::string title;
    std::string variable;
    auto mesh = [this](int index) { return SceneMesh(index); };
    mesh(2)->visible = false;
    auto* surface = surfaces[0];
    surface->Clear();
    for (int i = 16; i < 21; ++i) mesh(i)->visible = false;
    std::vector<std::string>& currentQuests = quests;
    int row = firstRow;
    int y = 13;
    for (int i = 16; i < 21 && row < static_cast<int>(currentQuests.size()); ++i, ++row, y += 24) {
        UiMesh* mark = mesh(i);
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
            mesh(2)->Place(215.0f, static_cast<float>(y + 29), 3.0f);
            mesh(2)->visible = true;
        }
        RECT area{36, y, 320, y + 21};
        surface->DrawTextIn(title, &area, 1, 0xA7D7E4, 0, 1);
    }
}

// --- What the window is opened for, and its help topic --------------------

void ObjectivesWindow::Setup(std::string_view zone, std::map<int, float>* newExitDistances, bool newTwoButtons, bool newListZones) {
    zoneName = zone;
    twoButtons = newTwoButtons;
    exitDistances = newExitDistances;
    listZones = newListZones;
}

const char* ObjectivesWindow::HelpTopic() { return twoButtons ? kGlobalMapTopic : kMissionTopic; }
