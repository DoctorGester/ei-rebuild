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
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <list>

namespace {
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";

} // namespace

void AIHeardCalls::Hear(const Vector3& point, int delay, std::string_view text) {
    std::erase_if(*this, [delay](const AICallRecord& call) { return call.hostileMask == 0 && call.delay >= delay; });
    push_back({point, delay, 0, std::string(text)});
}

void AIHeardCalls::AddClaim(std::uint32_t mask, int delay, std::string_view text) {
    // The point is left as the stack held it (saved as memory.callN.point); 0,0,0 is the
    // canonical value, which the harness gives game.exe too (harness_save_leftovers.cpp).
    push_back({Vector3{0.0f, 0.0f, 0.0f}, delay, static_cast<int>(mask), std::string(text)});
}

void AIHeardCalls::RemoveClaims() {
    std::erase_if(*this, [](const AICallRecord& call) { return call.hostileMask != 0; });
}

void AIMemory::HearCall(const Vector3& point, int delay, std::string_view text) { calls.Hear(point, delay, text); }
void AIMemory::AddClaim(std::uint32_t mask, int delay, std::string_view text) { calls.AddClaim(mask, delay, text); }

void AIViewer::Noise(CUnitServer* source, float loudness, int delay, const Vector3& at) {
    CUnit* unit = source->unit;
    std::list<CUnit*> units = map->sectorMap.FindUnitsInRadius(at, kNoiseRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (!other->IsHostileTo(unit)) continue;
        CUnitServer* control = other->ServerController();
        AIBrain* brain = &control->brain;
        // A unit that noticed something at least as loud as a call hears nothing.
        if (brain->memory.noticedLevel >= kNoticeCallLevel) continue;
        const double hearing = static_cast<double>(control->moveFactors[1]) * control->stats.perception[3].current;
        const float distance = vecmath::Distance<Sum3::XYZ>(at, other->localPosition_);
        EI_NAN_AUDIT(hearing, loudness, distance);
        if (hearing * loudness <= distance) continue;
        std::string text;
        if (console::g_filterMask & 1) {
            const double between = vecmath::Distance<Sum3::XYZ>(other->localPosition_, unit->localPosition_);
            text = str::Format(kHeardFormat, other->id_, unit->id_, between);
        }
        brain->memory.calls.Hear(unit->localPosition_, delay, text);
    }
}

AIEventPresence* AIViewer::StepNoise(const Vector3& position, CUnitServer* source) {
    const AITerrainType* ground = g_serverWorld.aiMap_.MaterialRecordAt(position);
    float loudness = source->moveFactors[0] * source->stats.conspicuity[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, AIBrain::kCallDelay, source->unit->localPosition_);
    return nullptr;
}
