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

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "library_trees.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

namespace {

using Arrivals = stlp::map<int, float>;

constexpr const char* kZoneVariable = "z.%s";


// Insert(hint, value) of the arrivals map (lib_trees::Arrivals').

// operator[]: lower_bound, and a new {exit, 0} entry
// inserted there when the key is not in the map.
float& Arrival(Arrivals& arrivals, int exit) {
    Arrivals::Node* at = arrivals.lower_bound(exit);
    if (at == arrivals.end() || exit < at->first) {
        const Arrivals::Value value = {exit, 0.0f};
        static_cast<lib_trees::Arrivals*>(&arrivals)->InsertHint(&at, at, value);
    }
    return at->second;
}

bool SameName(std::string_view a, std::string_view b) {
    return gamecrt::mbscmp(a, b) == 0;
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
    map->arrivals_.Clear();
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
    for (int i = 0; i < from->exits.size; ++i) {
        bool goesBeyond = false;
        MapRecord* zone = TheMapDatabase()->FindMap(from->exits.data[i].target.c_str());
        if (!zone) continue;
        float passTime = from->exits.data[i].passTime;
        if (SameName(zone->name, target_)) {
            found_ = true;
            int exit = from->exits.data[i].entrance;
            float time = path_.size() == 0 ? -1.0f : pathTime_;
            if (arrivals_.find(exit) == arrivals_.end()) {
                Arrival(arrivals_, exit) = time;
            } else {
                EI_NAN_AUDIT(pathTime_, Arrival(arrivals_, exit));
                if (pathTime_ < Arrival(arrivals_, exit)) Arrival(arrivals_, exit) = time;
            }
            if (beyondTarget_) continue;
            goesBeyond = true;
            beyondTarget_ = true;
        }
        // A zone already on the way ends this branch (and leaves beyondTarget_ set when the
        // branch had set it: kept).
        bool onWay = false;
        for (GlobalMapStep* step = path_.end(); step != path_.begin(); --step) {
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
EI_REWRITTEN(0x006312C0, CGlobalMap::FindPaths);

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
        if (index < 0 || index >= zoneCount_ || zones_.data[index].state == 0) return;
        interface_->PlaySound(OkSound(), 0);
        GlobalMapZone& zone = zones_.data[index];
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
    SearchWays(this, links_.data[link].name);
    if (!found_ || !MayTravelTo(this)) return;
    GamePassTime(Arrival(arrivals_, 0));
    ::RequestZone(links_.data[link].name.c_str(), 0);
}

EI_REWRITTEN(0x0062F950, CGlobalMap::OnLeftDown);
