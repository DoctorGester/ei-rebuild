// Check for the global map's zone array SetAtGrow (ui_global_map.cpp): 00631CC0. Ours only (it
// allocates and constructs): the array holds at least index + 1 zones within its allocation, the
// zone at `index` has the source's state, link and strings (equal text), and the zones before the
// old end keep their string buffers unless the array was reallocated (then the same texts).
// Scenarios: ui_camp (screen 4), frames_basic, render_present, render_cpuwindows.
#include "verify.h"

#include "../../src/game/ui_camp.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool SameText(const char* a, const char* b) { return std::strcmp(a ? a : "", b ? b : "") == 0; }
} // namespace

static void __fastcall ZonesSetAtGrowVerify(GlobalMapZoneArray* zones, void*, int index, const GlobalMapZone* zone) {
    std::vector<GlobalMapZone> before(zones->data, zones->data + zones->size);
    std::vector<std::vector<char>> names;
    for (const GlobalMapZone& z : before) names.emplace_back(z.name, z.name + std::strlen(z.name) + 1);
    GlobalMapZonesSetAtGrow(zones, nullptr, index, zone);
    bool ok = zones->size >= index + 1 && zones->size <= zones->maxSize;
    const GlobalMapZone& slot = zones->data[index];
    ok = ok && slot.state == zone->state && slot.link == zone->link && SameText(slot.name, zone->name) &&
         SameText(slot.title, zone->title);
    for (std::size_t i = 0; ok && i < before.size(); ++i)
        if (static_cast<int>(i) != index) ok = SameText(zones->data[i].name, names[i].data());
    for (int i = static_cast<int>(before.size()); ok && i < zones->size; ++i)
        if (i != index) ok = zones->data[i].state == 0 && zones->data[i].link == 0 && SameText(zones->data[i].name, "");
    ei::verify::Check("GlobalMapZonesSetAtGrow", true, ok, "index %d size %zu -> %d", index, before.size(), zones->size);
}
EI_VERIFY(0x00631CC0, ZonesSetAtGrowVerify, 0, s_unused);
