// The global map's way search: from the current zone through the zones the player may
// pass to the zone clicked (CGlobalMap::target_), recording per arrival exit of the target the
// shortest summed "#passtime". The click (Rebirth-patched) empties the results, sets
// the target and calls it with the local player's zone. See docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_camp.h"
#include "world_glue.h"
#include "game_strings.h"

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

bool SameName(std::string_view a, std::string_view b) {
    return str::Compare(a, b) == 0;
}

// --- The click ---

const char* MoveSound() { return "buttons\\globalmap\\move.wav"; }
const char* TransitSound() { return "buttons\\globalmap\\transit.wav"; }
const char* OkSound() { return "buttons\\globalmap\\ok.wav"; }

constexpr int kRegionTurnLeft = 0;
constexpr int kRegionTurnRight = 1;
constexpr int kRegionCamp = 2;
constexpr int kFirstLinkRegion = 3;
constexpr int kCampScreen = 5;

// The zone the local player is in.
MapRecord* PlayerZone() {
    return TheMapDatabase()->FindMap(LocalPlayerZone().c_str());
}

// The ways from the player's zone to `target`, the earlier results dropped.
void SearchWays(CGlobalMap* map, const std::string& target) {
    map->target_ = target;
    map->pathTime_ = 0.0f;
    map->arrivals_.clear();
    map->found_ = false;
    map->beyondTarget_ = false;
    map->FindPaths(PlayerZone());
}

bool MayTravelTo(CGlobalMap* map) {
    return map->MayTravel(0);
}

} // namespace

void CGlobalMap::FindPaths(MapRecord* from) {
    std::string variable;   // one string for every "z.<zone>" name
    for (int i = 0; i < std::ssize(from->exits); ++i) {
        bool goesBeyond = false;
        MapRecord* zone = TheMapDatabase()->FindMap(from->exits[i].target.c_str());
        if (!zone) continue;
        float passTime = from->exits[i].passTime;
        if (SameName(zone->name, target_)) {
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
            variable = str::Format(kZoneVariable, zone->name.c_str());
            int state = static_cast<int>(LocalPlayerVariable(variable.c_str()));   // truncated
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

void CGlobalMap::OnLeftDown(int, int x, int y) {
    if (dragging_) return;
    const int region = RegionAt(x, y);
    if (region == kRegionTurnLeft || region == kRegionTurnRight) {
        turning_ = region == kRegionTurnLeft ? -1 : 1;
        interface_->PlaySound(MoveSound(), 0);
        return;
    }
    if (region == kRegionCamp) {
        if (noCamp_ == 1) return;
        interface_->PlaySound(TransitSound(), 0);
        GameSetConstructionVariable(std::bit_cast<std::uint32_t>(0.0f));
        GameSendLeave04(kCampScreen);
        return;
    }
    int link;
    if (region >= kFirstLinkRegion) {
        interface_->PlaySound(OkSound(), 0);
        link = region - kFirstLinkRegion;
    } else {
        // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; Rebirth's
        // WideScreen takes the margin off its x (addon's code).
        const std::uint32_t pixel = backdrop_->PixelAt(x / 2 - widescreen::PickShift(), y / 2);
        const int index = static_cast<int>((pixel >> 4) & 0xF) - 1;
        if (index < 0 || index >= zoneCount_ || zones_[index].state == 0) return;
        interface_->PlaySound(OkSound(), 0);
        GlobalMapZone& zone = zones_[index];
        link = zone.link;
        if (!gipat_ || link < 0) {
            // A zone: its objectives and the ways there, to choose from (found or not).
            SearchWays(this, zone.name);
            if (!MayTravelTo(this)) return;
            auto* window = new CObjectivesWindow(interface_);
            window->Setup(target_.c_str(), &arrivals_, 1, 0);
            interface_->PushWindow(window, 1, 1);
            return;
        }
    }
    // A link: the party goes there at once when there is a way.
    SearchWays(this, links_[link].name);
    if (!found_ || !MayTravelTo(this)) return;
    GamePassTime(arrivals_[0]);
    ::RequestZone(links_[link].name.c_str(), 0);
}
