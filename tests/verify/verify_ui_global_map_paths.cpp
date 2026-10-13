// Checks for the global map's pick picture (00630670, ui_global_map_places.cpp) and way search
// (006312C0, ui_global_map_paths.cpp).
//
// RefreshSelection renders a frame (a frame step) and copies the screen: ours only. It runs from
// the map's Update after Create, a right-button drag and a closed window above (ui_camp: `screen
// 4`, the drag, Esc / Esc). After it: the zone markers carry their pick colours, everything else
// is shown again but the last two scene objects, the display colour is back, the first surface
// hidden and the second shown, and the zone markers' textures and plain flags are SelectZone(-1)'s.
//
// FindPaths: side by side when the click calls it (the results map empty, no way yet): the
// original first (its own recursive calls reach this wrapper and go straight to the original),
// the results recorded, the map emptied (00631710) and the flags put back, then ours; the entries
// (arrival exit, time), found_ and the flags must agree. A call with results already present runs
// ours only. Reached by a click on a zone or link of the map (0062F950); no scenario clicks one.
//
// OnLeftDown (0062F950, input slot 2; Rebirth-patched: the vanilla launch only): ours only (it
// plays sounds, asks the server for the camp or a zone, or opens the objectives window). Read
// back: nothing while dragging; the turn buttons (regions 0 / 1) set turning_ to -1 / +1 and
// nothing else changes it. Scenario: ui_camp opens the map; whether it presses the left button
// there has not been run (the worker cannot run the game).
#include "verify.h"

#include "../../src/game/originals_render.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void* s_unused;

std::uint32_t PickColour(int i) {
    std::uint32_t level = 0x18u + 0x10u * static_cast<std::uint32_t>(i);
    return ((((level | 0xFFFFFF00u) << 8) | level) << 8) | level;
}

std::uint32_t* DisplayColour() { return reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(DisplayObject()) + 0x14); }

} // namespace

// CGlobalMap::RefreshSelection (00630670): ours only.
static void __fastcall RefreshSelectionVerify(CGlobalMap* self, void*) {
    if (!self->backdrop_) {
        self->CGlobalMap::RefreshSelection();
        return;
    }
    std::uint32_t colour = *DisplayColour();
    self->CGlobalMap::RefreshSelection();
    const char* name = "CGlobalMap::RefreshSelection";
    ei::verify::Check(name, colour, *DisplayColour(), "display colour");
    if (self->surfaces_.size >= 2) {
        ei::verify::Check(name, true, !self->surfaces_.data[0]->Shown() && self->surfaces_.data[1]->Shown(),
                          "surfaces shown %d %d", self->surfaces_.data[0]->Shown() ? 1 : 0,
                          self->surfaces_.data[1]->Shown() ? 1 : 0);
    }
    for (int i = 0; i < self->sceneObjects_.size; ++i) {
        if (i >= 3 && i < self->zoneCount_ + 3) continue;
        int expected = i == self->markerCount_ + 6 || i == self->markerCount_ + 7 ? 0 : 1;
        ei::verify::Check(name, expected, self->SceneMesh(i)->visible_, "scene object %d of %d", i,
                          self->sceneObjects_.size);
    }
    for (int i = 0; i < self->zoneCount_; ++i) {
        CUiMesh* marker = self->Marker(i);
        int state = self->zones_.data[i].state;
        bool selectable = state >= 0 && state <= 2;
        CEngineObject* texture = state == 0 ? self->objects_.data[2] : selectable ? self->objects_.data[0] : nullptr;
        ei::verify::Check(name, true,
                          marker->plainColour_ == PickColour(i) && marker->texture_ == texture &&
                              ((marker->flags_ & 0x100u) != 0) == !selectable,
                          "zone %d state %d colour %08X flags %08X", i, state, marker->plainColour_, marker->flags_);
    }
}
EI_VERIFY(0x00630670, RefreshSelectionVerify, 0, s_unused);

namespace {

using Arrivals = stlp::map<int, float>;

// 00631710 (thiscall, plain RET): clear() of the arrivals map.
__declspec(naked) void __fastcall ArrivalsClear(Arrivals*) EI_ORIGINAL(0x00631710)

struct Entry {
    int exit;
    float time;
};

void Collect(const Arrivals::Node* node, const Arrivals::Node* header, std::vector<Entry>& out) {
    if (!node || node == header) return;
    Collect(node->left, header, out);
    out.push_back(Entry{node->first, node->second});
    Collect(node->right, header, out);
}

struct SearchState {
    std::vector<Entry> entries;
    bool found;
    bool beyond;
    std::size_t steps;
    float time;

    static SearchState Take(const CGlobalMap* map) {
        SearchState s;
        Collect(map->arrivals_.end()->parent, map->arrivals_.end(), s.entries);
        s.found = map->found_;
        s.beyond = map->beyondTarget_;
        s.steps = map->path_.size();
        s.time = map->pathTime_;
        return s;
    }
};

bool s_inOriginal = false;
void(__fastcall* s_findPaths)(CGlobalMap*, void*, MapRecord*);

} // namespace

// CGlobalMap::FindPaths (006312C0). PUSH -1; PUSH imm32 (7).
static void __fastcall FindPathsVerify(CGlobalMap* self, void*, MapRecord* from) {
    if (s_inOriginal) {
        s_findPaths(self, nullptr, from);
        return;
    }
    if (self->arrivals_.size() != 0 || self->path_.size() != 0) {
        self->CGlobalMap::FindPaths(from);
        return;
    }
    bool found = self->found_;
    bool beyond = self->beyondTarget_;
    float time = self->pathTime_;
    s_inOriginal = true;
    s_findPaths(self, nullptr, from);
    s_inOriginal = false;
    SearchState expected = SearchState::Take(self);
    ArrivalsClear(&self->arrivals_);
    self->found_ = found;
    self->beyondTarget_ = beyond;
    self->pathTime_ = time;
    self->CGlobalMap::FindPaths(from);
    SearchState actual = SearchState::Take(self);
    const char* name = "CGlobalMap::FindPaths";
    ei::verify::Check(name, true,
                      expected.entries.size() == actual.entries.size() && expected.found == actual.found &&
                          expected.beyond == actual.beyond && expected.steps == actual.steps,
                      "from %s target %s: entries %d / %d found %d / %d beyond %d / %d", from->name, self->target_,
                      static_cast<int>(expected.entries.size()), static_cast<int>(actual.entries.size()),
                      expected.found ? 1 : 0, actual.found ? 1 : 0, expected.beyond ? 1 : 0, actual.beyond ? 1 : 0);
    ei::verify::Check(name, expected.time, actual.time, "time after");
    if (expected.entries.size() != actual.entries.size()) return;
    for (std::size_t i = 0; i < expected.entries.size(); ++i) {
        ei::verify::Check(name, expected.entries[i].exit, actual.entries[i].exit, "entry %d exit", static_cast<int>(i));
        ei::verify::CheckNear(name, expected.entries[i].time, actual.entries[i].time, "exit %d time",
                              expected.entries[i].exit);
    }
}
EI_VERIFY(0x006312C0, FindPathsVerify, 7, s_findPaths);

// CGlobalMap::OnLeftDown (0062F950): ours only.
static void __fastcall GlobalMapLeftDownVerify(void* input, void*, int buttons, int x, int y) {
    CGlobalMap* self = WindowFromInput<CGlobalMap>(input);
    const bool dragging = self->dragging_;
    const int turning = self->turning_;
    const int region = self->RegionAt(x, y);
    self->OnLeftDown(buttons, x, y);
    const int expected = dragging ? turning : region == 0 ? -1 : region == 1 ? 1 : turning;
    ei::verify::Check("CGlobalMap::OnLeftDown", expected, self->turning_, "region %d dragging %d", region,
                      dragging ? 1 : 0);
}
EI_VERIFY(0x0062F950, GlobalMapLeftDownVerify, 0, s_unused);
