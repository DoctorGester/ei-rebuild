// The global map's way search: from the current zone through the zones the player may
// pass to the zone clicked (GlobalMap::target), recording per arrival exit of the target the
// shortest summed "#passtime". The click (Rebirth-patched) empties the results, sets
// the target and calls it with the local player's zone. See docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "game/ui/ui_camp.h"

#include "game/console/console.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/map/map_database.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_objectives.h"

#include <algorithm>
#include <memory>
#include "game/ui/ui_screens.h"
#include "game/world/world.h"
#include "game/players/player_client.h"
#include "lib/containers.h"
#include "lib/str.h"

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
                if (pathTime < arrivals[exit]) arrivals[exit] = time;
            }
            if (beyondTarget) continue;
            goesBeyond = true;
            beyondTarget = true;
        }
        // A zone already on the way ends this branch (and leaves beyondTarget set when the
        // branch had set it: kept).
        if (std::any_of(path.begin(), path.end(), [zone](const GlobalMapStep& step) { return step.zone == zone; })) continue;
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

void GlobalMap::LeftDown(UiEvent& event) {
    if (dragging) return;
    UiRegion* region = RegionUnder(event.at);
    if (region == turnLeftButton || region == turnRightButton) {
        turning = region == turnLeftButton ? -1 : 1;
        g_interface.PlayInterfaceSound(kMoveSound);
        return;
    }
    if (region == campButton) {
        if (noCamp) return;
        g_interface.PlayInterfaceSound(kTransitSound);
        g_serverWorld.SetConstructionVariable(0.0f);
        g_interface.closeAll = 1;
        g_clientWorld.LocalView()->RequestGameState(kCampScreen);
        return;
    }
    int link = RegionIndex(linkAreas, region);
    if (link >= 0) {
        g_interface.PlayInterfaceSound(kOkSound);
    } else {
        const int index = ZoneAt(event.at);
        if (index < 0 || zones[index].state == 0) return;
        g_interface.PlayInterfaceSound(kOkSound);
        GlobalMapZone& zone = zones[index];
        link = zone.link;
        if (!gipat || link < 0) {
            // A zone: its objectives and the ways there, to choose from (found or not).
            SearchWays(zone.name);
            auto window = std::make_unique<ObjectivesWindow>();
            window->Setup(target, &arrivals, true, false);
            g_interface.PushWindow(std::move(window), WindowBackdrop::FirstWindow);
            return;
        }
    }
    // A link: the party goes there at once when there is a way.
    SearchWays(links[link].name);
    if (!found) return;
    g_serverWorld.PassTime(arrivals[0]);
    ::RequestZone(links[link].name, 0);
}

// --- Ui global map places ----------------------------------------------------------------------------

// The global map's (GlobalMap, ui_camp.h) places: the titles and positions of its allod, zones and quests
// from the texts and map.txt, the zone selection's marker colours and its refresh. See docs/notes/ui.md
// ("Camp, global map, hero screens").

namespace {

constexpr const char* kMapText = "map.txt";
constexpr const char* kPositionLine = "#position";
constexpr const char* kMissingNumber = "0";

} // namespace

// --- titles and positions ------------------------------------------------------------------------

std::string GlobalMap::PlaceTitle(std::string_view prefix, std::string_view name) {
    TextManager* texts = g_clientWorld.texts.get();
    std::string title;
    std::string key(prefix);
    key += name;
    if (texts->Open(key)) title = texts->ReadLine(true);
    return title;
}

std::optional<Vector3> GlobalMap::MapPosition(std::string_view prefix, std::string_view name, bool positionLine) {
    TextManager* texts = g_clientWorld.texts.get();
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
            position = Vector3{str::ToFloat(words[0]), str::ToFloat(words[1]),
                               str::ToFloat(words[2])};
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
        UiMesh* marker = zoneMarkers[i];
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
        UiMesh* marker = linkMarkers[i];
        for (int k = 0; k < 3; ++k) marker->tint[k] = i == link ? fog[k] : 1.0f;
    }
}

// --- the refresh ---------------------------------------------------------------------------------

// What the original's pick picture step left behind (the click now finds the zone by its marker's faces, ZoneAt):
// the tip hidden, the title shown, every sprite but the zone markers shown (the glow layer and a hidden camp button
// too, as there), the tip's two sprites hidden, nothing selected.
void GlobalMap::RefreshSelection() {
    surfaces[0]->Hide();
    surfaces[1]->Show();
    for (UiMesh* sprite : sceneObjects)
        if (std::ranges::find(zoneMarkers, sprite) == zoneMarkers.end()) sprite->visible = true;
    tipBack->visible = false;
    tipFrame->visible = false;
    SelectZone(-1, false);
}
