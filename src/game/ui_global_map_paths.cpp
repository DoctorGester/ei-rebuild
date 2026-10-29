// The global map's way search: from the current zone through the zones the player may
// pass to the zone clicked (CGlobalMap::target_), recording per arrival exit of the target the
// shortest summed "#passtime". The click (Rebirth-patched) empties the results, sets
// the target and calls it with the local player's zone. See docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_camp.h"
#include "world_glue.h"

#include "object_tables.h"
#include "ui.h"
#include "ui_screens.h"
#include "ui_widescreen.h"
#include "world.h"

#include "ei/rebirth.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

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

void CGlobalMap::SearchWays(const std::string& target) {
    target_ = target;
    pathTime_ = 0.0f;
    arrivals_.clear();
    found_ = false;
    beyondTarget_ = false;
    FindPaths(g_mapDatabase.FindMap(LocalPlayerZone()));
}

void CGlobalMap::FindPaths(MapRecord* from) {
    std::string variable;   // one string for every "z.<zone>" name
    for (int i = 0; i < std::ssize(from->exits); ++i) {
        bool goesBeyond = false;
        MapRecord* zone = g_mapDatabase.FindMap(from->exits[i].target);
        if (!zone) continue;
        float passTime = from->exits[i].passTime;
        if (str::Compare(zone->name, target_) == 0) {
            found_ = true;
            int exit = from->exits[i].entrance;
            float time = path_.size() == 0 ? -1.0f : pathTime_;
            if (arrivals_.find(exit) == arrivals_.end()) {
                arrivals_[exit] = time;
            } else {
                EI_NAN_AUDIT(pathTime_, arrivals_[exit]);
                if (pathTime_ < arrivals_[exit]) arrivals_[exit] = time;
            }
            if (beyondTarget_) continue;
            goesBeyond = true;
            beyondTarget_ = true;
        }
        // A zone already on the way ends this branch (and leaves beyondTarget_ set when the
        // branch had set it: kept).
        bool onWay = false;
        for (GlobalMapStep* step = path_.data() + path_.size(); step != path_.data(); --step) {
            if (step[-1].zone == zone) {
                onWay = true;
                break;
            }
        }
        if (onWay) continue;
        bool passable = false;
        if (zone->kind == 0 || zone->kind == 1 || zone->kind == 2) {
            variable = str::Format(kZoneVariable, zone->name);
            int state = static_cast<int>(LocalPlayerVariable(variable));   // truncated
            passable = zone->kind == 0 ? state == 2 : zone->kind == 1 ? state != 1 : state != 0;
        }
        if (passable) {
            path_.push_back(GlobalMapStep{from, i});
            float saved = pathTime_;
            pathTime_ = passTime + pathTime_;
            FindPaths(zone);
            pathTime_ = saved;
            path_.pop_back();
        }
        if (goesBeyond) beyondTarget_ = false;
    }
}

void CGlobalMap::LeftDown(int, int x, int y) {
    if (dragging_) return;
    const int region = RegionAt(x, y);
    if (region == kRegionTurnLeft || region == kRegionTurnRight) {
        turning_ = region == kRegionTurnLeft ? -1 : 1;
        interface_->PlaySound(kMoveSound);
        return;
    }
    if (region == kRegionCamp) {
        if (noCamp_) return;
        interface_->PlaySound(kTransitSound);
        GameSetConstructionVariable(std::bit_cast<std::uint32_t>(0.0f));
        GameSendLeave04(kCampScreen);
        return;
    }
    int link;
    if (region >= kFirstLinkRegion) {
        interface_->PlaySound(kOkSound);
        link = region - kFirstLinkRegion;
    } else {
        // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; Rebirth's
        // WideScreen takes the margin off its x (addon's code).
        const std::uint32_t pixel = backdrop_->PixelAt(x / 2 - widescreen::PickShift(), y / 2);
        const int index = static_cast<int>((pixel >> 4) & 0xF) - 1;
        if (index < 0 || index >= std::ssize(zones_) || zones_[index].state == 0) return;
        interface_->PlaySound(kOkSound);
        GlobalMapZone& zone = zones_[index];
        link = zone.link;
        if (!gipat_ || link < 0) {
            // A zone: its objectives and the ways there, to choose from (found or not).
            SearchWays(zone.name);
            auto* window = new CObjectivesWindow(interface_);
            window->Setup(target_, &arrivals_, true, false);
            interface_->PushWindow(window, 1, 1);
            return;
        }
    }
    // A link: the party goes there at once when there is a way.
    SearchWays(links_[link].name);
    if (!found_) return;
    GamePassTime(arrivals_[0]);
    ::RequestZone(links_[link].name, 0);
}
