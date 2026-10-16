// The global map's way search (006312C0): from the current zone through the zones the player may
// pass to the zone clicked (CGlobalMap::target_), recording per arrival exit of the target the
// shortest summed "#passtime". The click (0062F950, Rebirth-patched) empties the results, sets
// the target and calls it with the local player's zone. See docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_camp.h"
#include "game_strings.h"

#include "object_tables.h"
#include "originals_ui.h"
#include "ui.h"
#include "ui_screens.h"
#include "ui_widescreen.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>

namespace {

using Arrivals = stlp::map<int, float>;

constexpr const char* kZoneVariable = "z.%s";   // 00794148

// value_type of the arrivals map.
struct ArrivalPair {
    int exit;
    float time;
};

// 006316E0 (thiscall, RET 0xC): insert(hint, value) of the arrivals map; the node in `*out`,
// returns `out`.
__declspec(naked) Arrivals::Node** __fastcall ArrivalsInsert(Arrivals*, void*, Arrivals::Node** /*out*/,
                                                             Arrivals::Node* /*hint*/, const ArrivalPair* /*value*/)
    EI_ORIGINAL(0x006316E0)

// operator[] as the original inlines it: lower_bound (00631C80), and a new {exit, 0} entry
// inserted there when the key is not in the map.
float& Arrival(Arrivals& arrivals, int exit) {
    Arrivals::Node* at = arrivals.lower_bound(exit);
    if (at == arrivals.end() || exit < at->first) {
        ArrivalPair value = {exit, 0.0f};
        ArrivalsInsert(&arrivals, nullptr, &at, at, &value);
    }
    return at->second;
}

bool SameName(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}

// --- The click (0062F950) ---

const char* MoveSound() { return game::Text(0x00794180); }      // "buttons\globalmap\move.wav"
const char* TransitSound() { return game::Text(0x0079372C); }   // "buttons\globalmap\transit.wav"
const char* OkSound() { return game::Text(0x0079419C); }        // "buttons\globalmap\ok.wav"
// The client's glue objects (their members do not read them).
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
void* UnitGlue() { return reinterpret_cast<void*>(0x007AFEA6); }

constexpr int kRegionTurnLeft = 0;
constexpr int kRegionTurnRight = 1;
constexpr int kRegionCamp = 2;
constexpr int kFirstLinkRegion = 3;
constexpr int kCampScreen = 5;
constexpr std::size_t kObjectivesSize = 0x1F0;

mfc::CString& AsString(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }

// The zone the local player is in.
MapRecord* PlayerZone() {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* name = LocalPlayerName(reinterpret_cast<mfc::CString*>(raw));
    MapRecord* zone = GameMapDatabase()->FindMap(*name);
    name->~CString();
    return zone;
}

// The ways from the player's zone to `target`, the earlier results dropped.
void SearchWays(CGlobalMap* map, const mfc::CString& target) {
    AsString(map->target_) = target;
    map->pathTime_ = 0.0f;
    map->arrivals_.Clear();
    map->found_ = false;
    map->beyondTarget_ = false;
    map->FindPaths(PlayerZone());
}

bool MayTravelTo(CGlobalMap* map) {
    return map->MayTravel(static_cast<int>(reinterpret_cast<std::uintptr_t>(map->target_)));
}

} // namespace

void CGlobalMap::FindPaths(MapRecord* from) {
    mfc::CString variable;   // one string for every "z.<zone>" name, as in the original
    for (int i = 0; i < from->exits.size; ++i) {
        bool goesBeyond = false;
        MapRecord* zone = GameMapDatabase()->FindMap(from->exits.data[i].target);
        if (!zone) continue;
        float passTime = from->exits.data[i].passTime;
        if (SameName(zone->name, target_)) {
            found_ = true;
            int exit = from->exits.data[i].targetExit;
            float time = path_.size() == 0 ? -1.0f : pathTime_;
            if (arrivals_.find(exit) == arrivals_.end()) {
                Arrival(arrivals_, exit) = time;
            } else if (!(pathTime_ >= Arrival(arrivals_, exit))) {   // smaller, or unordered
                Arrival(arrivals_, exit) = time;
            }
            if (beyondTarget_) continue;
            goesBeyond = true;
            beyondTarget_ = true;
        }
        // A zone already on the way ends this branch (and leaves beyondTarget_ set when the
        // branch had set it: kept from the original).
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
            variable.Format(kZoneVariable, zone->name);
            int state = static_cast<int>(LocalPlayerVariable(variable));   // _ftol: truncated
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
EI_REPLACE_METHOD(0x006312C0, CGlobalMap::FindPaths);

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
        GameSetConstructorOriginal(UnitGlue(), nullptr, 0.0f);
        GameRequestScreenOriginal(ClientGlue(), nullptr, kCampScreen);
        return;
    }
    int link;
    if (region >= kFirstLinkRegion) {
        interface_->PlaySound(OkSound(), 0);
        link = region - kFirstLinkRegion;
    } else {
        // The pick picture (RefreshSelection) holds zone i as the colour 0x18 + 0x10 * i; Rebirth's
        // WideScreen takes the margin off its x (0062FB20: addon's code at 005FD237).
        const std::uint32_t pixel = backdrop_->PixelAt(x / 2 - widescreen::PickShift(), y / 2);
        const int index = static_cast<int>((pixel >> 4) & 0xF) - 1;
        if (index < 0 || index >= zoneCount_ || zones_.data[index].state == 0) return;
        interface_->PlaySound(OkSound(), 0);
        GlobalMapZone& zone = zones_.data[index];
        link = zone.link;
        if (!gipat_ || link < 0) {
            // A zone: its objectives and the ways there, to choose from (found or not).
            SearchWays(this, AsString(zone.name));
            if (!MayTravelTo(this)) return;
            void* memory = gamecrt::operator_new(kObjectivesSize);
            void* window = memory ? static_cast<CObjectivesWindow*>(memory)->Construct(interface_) : nullptr;
            static_cast<CObjectivesWindow*>(window)->Setup(target_, &arrivals_, 1, 0);
            InterfacePushWindowOriginal(interface_, nullptr, window, 1, 1);
            return;
        }
    }
    // A link: the party goes there at once when there is a way.
    SearchWays(this, AsString(links_.data[link].name));
    if (!found_ || !MayTravelTo(this)) return;
    PassTravelTimeOriginal(UnitGlue(), nullptr, Arrival(arrivals_, 0));
    ::RequestZone(links_.data[link].name, 0);
}

namespace {
// Rebirth-patched (the shim stays a named function so check_patched_calls sees it).
void __fastcall GlobalMapLeftDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnLeftDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x0062F950, GlobalMapLeftDown);
