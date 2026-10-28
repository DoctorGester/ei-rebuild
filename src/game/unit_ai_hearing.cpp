// What the AI hears: the heard calls' Hear / AddClaim / RemoveClaims, the perception memory's HearCall /
// AddClaim and the AI events' Noise. See unit_ai.h and docs/notes/unit_ai.md.
#include "unit_ai.h"
#include "combat.h"

#include "ai_map.h"
#include "console.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <list>

namespace {
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
constexpr int kAlertedLevel = 900;      // a unit noticing something at this level or above hears nothing
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";

} // namespace

void AIHeardCalls::Hear(const Vector3* point, int kind, const char* text) {
    std::erase_if(*this, [kind](const AICallRecord& call) { return call.hostileMask == 0 && call.kind >= kind; });
    push_back({*point, kind, 0, text});
}

void AIHeardCalls::AddClaim(std::uint32_t mask, int kind, const char* text) {
    // The point is left as the stack held it (saved as memory.callN.point); 0,0,0 is the
    // canonical value, which the harness gives game.exe too (harness_save_leftovers.cpp).
    push_back({Vector3{0.0f, 0.0f, 0.0f}, kind, static_cast<int>(mask), text});
}

void AIHeardCalls::RemoveClaims() {
    std::erase_if(*this, [](const AICallRecord& call) { return call.hostileMask != 0; });
}

void AIMemory::HearCall(const Vector3* point, int kind, const char* text) { calls.Hear(point, kind, text); }
void AIMemory::AddClaim(std::uint32_t mask, int kind, const char* text) { calls.AddClaim(mask, kind, text); }

void AIViewer::Noise(CUnitServer* source, float loudness, int kind, const Vector3* at) {
    std::list<CUnit*> units{};
    CUnit* unit = source->unit;
    const Vector3* point = at ? at : &unit->localPosition_;
    map->SectorMap()->FindUnitsInRadius(point, &units, kNoiseRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (!other->IsHostileTo(unit)) continue;
        CUnitServer* control = other->ServerController();
        AIBrain* brain = control->Brain();
        if (brain->memory.noticedLevel >= kAlertedLevel) continue;
        const double hearing = static_cast<double>(control->moveFactors[1]) * control->stats.valuesB0[3].current;
        const float distance = vecmath::Distance<Sum3::XYZ>(*point, other->localPosition_);
        EI_NAN_AUDIT(hearing, loudness, distance);
        if (hearing * loudness <= distance) continue;
        std::string text;
        if (console::g_filterMask & 1) {
            const double between = vecmath::Distance<Sum3::XYZ>(other->localPosition_, unit->localPosition_);
            text = str::Format(kHeardFormat, other->id_, unit->id_, between);
        }
        brain->memory.calls.Hear(&unit->localPosition_, kind, text.c_str());
    }
    // The list's RemoveAll: no element destruction.
    units.clear();
}

AIEventPresence* AIViewer::StepNoise(const Vector3* position, CUnitServer* source) {
    constexpr int kStepNoise = 0x1A;
    const AITerrainType* ground = TheServerWorld()->AIMap()->MaterialRecordAt(position);
    float loudness = source->moveFactors[0] * source->stats.valuesE0[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, kStepNoise, nullptr);
    return nullptr;
}
