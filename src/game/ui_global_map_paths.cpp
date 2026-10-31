// The global map's way search: from the current zone through the zones the player may
// pass to the zone clicked (GlobalMap::target), recording per arrival exit of the target the
// shortest summed "#passtime". The click (Rebirth-patched) empties the results, sets
// the target and calls it with the local player's zone. See docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_camp.h"

#include "app.h"
#include "console.h"
#include "game_singletons.h"
#include "model.h"
#include "object_tables.h"
#include "text_manager.h"
#include "ui.h"
#include "ui_screens.h"
#include "screen_layout.h"
#include "world.h"
#include "world_glue.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "render/render_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* kZoneVariable = "z.%s";

// --- The click ---

constexpr const char* kMoveSound = "buttons\\globalmap\\move.wav";
constexpr const char* kTransitSound = "buttons\\globalmap\\transit.wav";
constexpr const char* kOkSound = "buttons\\globalmap\\ok.wav";

constexpr int kRegionTurnLeft = 0;
constexpr int kRegionTurnRight = 1;
constexpr int kRegionCamp = 2;
constexpr int kFirstLinkRegion = 3;
constexpr int kCampScreen = 5;

} // namespace

void GlobalMap::SearchWays(const std::string& destination) {
    target = destination;
    pathTime = 0.0f;
    arrivals.clear();
    found = false;
    beyondTarget = false;
    FindPaths(g_mapDatabase.FindMap(LocalPlayerZone()));
}

void GlobalMap::FindPaths(MapRecord* from) {
    std::string variable;   // one string for every "z.<zone>" name
    for (int i = 0; i < std::ssize(from->exits); ++i) {
        bool goesBeyond = false;
        MapRecord* zone = g_mapDatabase.FindMap(from->exits[i].target);
        if (!zone) continue;
        float passTime = from->exits[i].passTime;
        if (str::Compare(zone->name, target) == 0) {
            found = true;
            int exit = from->exits[i].entrance;
            float time = path.size() == 0 ? -1.0f : pathTime;
            if (arrivals.find(exit) == arrivals.end()) {
                arrivals[exit] = time;
            } else {
                EI_NAN_AUDIT(pathTime, arrivals[exit]);
                if (pathTime < arrivals[exit]) arrivals[exit] = time;
            }
            if (beyondTarget) continue;
            goesBeyond = true;
            beyondTarget = true;
        }
        // A zone already on the way ends this branch (and leaves beyondTarget set when the
        // branch had set it: kept).
        bool onWay = false;
        for (GlobalMapStep* step = path.data() + path.size(); step != path.data(); --step) {
            if (step[-1].zone == zone) {
                onWay = true;
                break;
            }
        }
        if (onWay) continue;
        bool passable = false;
        if (zone->kind == 0 || zone->kind == 1 || zone->kind == 2) {
            variable = str::Format(kZoneVariable, zone->name);
            // Truncated as _ftol: the low half of the int64 truncation (0 for a NaN).
            int state = static_cast<int>(static_cast<std::int64_t>(LocalPlayerVariable(variable)));
            passable = zone->kind == 0 ? state == 2 : zone->kind == 1 ? state != 1 : state != 0;
        }
        if (passable) {
            path.push_back(GlobalMapStep{from, i});
            float saved = pathTime;
            pathTime = passTime + pathTime;
            FindPaths(zone);
            pathTime = saved;
            path.pop_back();
        }
        if (goesBeyond) beyondTarget = false;
    }
}

void GlobalMap::LeftDown(int, int x, int y) {
    if (dragging) return;
    const int region = RegionAt(x, y);
    if (region == kRegionTurnLeft || region == kRegionTurnRight) {
        turning = region == kRegionTurnLeft ? -1 : 1;
        ui->PlayInterfaceSound(kMoveSound);
        return;
    }
    if (region == kRegionCamp) {
        if (noCamp) return;
        ui->PlayInterfaceSound(kTransitSound);
        GameSetConstructionVariable(std::bit_cast<std::uint32_t>(0.0f));
        GameSendLeave04(kCampScreen);
        return;
    }
    int link;
    if (region >= kFirstLinkRegion) {
        ui->PlayInterfaceSound(kOkSound);
        link = region - kFirstLinkRegion;
    } else {
        // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; the window's 4:3
        // part's margins come off (Rebirth's WideScreen took the margin off x).
        const Point shift = screen_layout::PickShift();
        const std::uint32_t pixel = backdrop->PixelAt(x / 2 - shift.x, y / 2 - shift.y);
        const int index = static_cast<int>((pixel >> 4) & 0xF) - 1;
        if (index < 0 || index >= std::ssize(zones) || zones[index].state == 0) return;
        ui->PlayInterfaceSound(kOkSound);
        GlobalMapZone& zone = zones[index];
        link = zone.link;
        if (!gipat || link < 0) {
            // A zone: its objectives and the ways there, to choose from (found or not).
            SearchWays(zone.name);
            auto* window = new ObjectivesWindow(ui);
            window->Setup(target, &arrivals, true, false);
            ui->PushWindow(window, true, true);
            return;
        }
    }
    // A link: the party goes there at once when there is a way.
    SearchWays(links[link].name);
    if (!found) return;
    GamePassTime(arrivals[0]);
    ::RequestZone(links[link].name, 0);
}

// --- Ui global map places ----------------------------------------------------------------------------

// The global map's (GlobalMap, ui_camp.h) places: the titles and positions of its allod, zones and quests
// from the texts and map.txt, the zone selection's marker colours and the pick picture. See docs/notes/ui.md
// ("Camp, global map, hero screens").

namespace {

constexpr const char* kMapText = "map.txt";
constexpr const char* kPositionLine = "#position";
constexpr const char* kMissingNumber = "0";

} // namespace

// --- titles and positions ------------------------------------------------------------------------

std::string GlobalMap::PlaceTitle(std::string_view prefix, std::string_view name) {
    TextManager* texts = g_clientWorld.texts;
    std::string title;
    std::string key(prefix);
    key += name;
    if (texts->Open(key)) title = texts->ReadLine(true);
    return title;
}

std::optional<Vector3> GlobalMap::MapPosition(std::string_view prefix, std::string_view name, bool positionLine) {
    TextManager* texts = g_clientWorld.texts;
    std::string key;
    std::vector<std::string> words;
    texts->Open(kMapText);
    key = prefix;
    key += name;
    std::optional<Vector3> position;
    if (texts->FindLine(key) && (!positionLine || texts->FindLine(kPositionLine))) {
        texts->NextLine();
        if (!texts->IsPastEnd(texts->cursor)) {
            words = texts->Split(true);
            while (words.size() < 3) words.push_back(kMissingNumber);
            position = Vector3{static_cast<float>(str::ToDouble(words[0])), static_cast<float>(str::ToDouble(words[1])),
                               static_cast<float>(str::ToDouble(words[2]))};
        }
    }
    texts->Close();
    return position;
}

// --- the selected zone ---------------------------------------------------------------------------

void GlobalMap::SelectZone(int index, bool isLink) {
    int zone = -1;
    int link = -1;
    if (!isLink) {
        zone = index;
        if (gipat && index >= 0 && zones[index].link >= 0) link = zones[index].link;
    } else {
        link = index;
        if (gipat && index >= 0 && links[index].zone >= 0) zone = links[index].zone;
    }
    for (int i = 0; i < std::ssize(zones); ++i) {
        UiMesh* marker = Marker(i);
        int state = zones[i].state;
        if (state == 0) {
            marker->texture = objects[2];
        } else if (state == 1 || state == 2) {
            marker->texture = objects[0];
            float scale = state == 1 ? 1.0f : 0.5f;
            for (int k = 0; k < 3; ++k) marker->tint[k] = i == zone ? fog[k] * scale : scale;
        } else {
            continue;
        }
        marker->flags &= ~FigureMeshFlag::Flat;
    }
    for (int i = 0; i < std::ssize(links); ++i) {
        UiMesh* marker = Marker(lib::Count(zones) + lib::Count(quests) + i);
        for (int k = 0; k < 3; ++k) marker->tint[k] = i == link ? fog[k] : 1.0f;
    }
}

// --- the pick picture ----------------------------------------------------------------------------

// Scene objects 0..2 (arrow, base, island) and everything after the zone markers (quests, links,
// layers and the rest) shown or hidden.
void GlobalMap::ShowAllButZones(bool visible) {
    for (int i = 0; i < 3; ++i) SceneMesh(i)->visible = visible;
    for (int i = lib::Count(zones) + 3; i < lib::Count(sceneObjects); ++i) SceneMesh(i)->visible = visible;
}

void GlobalMap::RefreshSelection() {
    if (!backdrop) return;
    for (int i = 0; i < std::ssize(zones); ++i) {
        UiMesh* marker = Marker(i);
        std::uint32_t level = 0x18u + 0x10u * static_cast<std::uint32_t>(i);
        marker->texture = nullptr;
        // Opaque grey (from the 16th zone on the level's ninth bit spills into the next channel, as in the original).
        marker->flatColour = 0xFF000000u | level << 16 | level << 8 | level;
        marker->flags |= FigureMeshFlag::Flat;
    }
    surfaces[1]->Hide(false);
    surfaces[0]->Hide(false);
    ShowAllButZones(false);
    std::uint32_t colour = g_sceneRenderer.fogColour;
    g_sceneRenderer.fogColour = 0;
    app::FrameStep(false, false);
    g_sceneRenderer.fogColour = colour;
    surfaces[1]->Show();
    ShowAllButZones(true);
    SceneMesh(MarkerCount() + 6)->visible = false;
    SceneMesh(MarkerCount() + 7)->visible = false;
    backdrop->CaptureScreen();
    SelectZone(-1, false);
}
