// What the AI hears: the heard calls' Hear / AddClaim / RemoveClaims, the perception memory's HearCall /
// AddClaim and the AI events' Noise. See unit_ai.h and docs/notes/unit_ai.md.
#include "unit_ai.h"
#include "combat.h"

#include "ai_map.h"
#include "console.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
constexpr int kAlertedLevel = 900;      // a unit noticing something at this level or above hears nothing
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";

// The inlined RemoveAt(index, 1): the record's text freed, the tail moved down.
void RemoveCall(AIHeardCalls* calls, int index) {
    calls->data[index].Destruct();
    int after = calls->size - index - 1;
    if (after != 0) std::memmove(&calls->data[index], &calls->data[index + 1], static_cast<std::size_t>(after) * sizeof(AICallRecord));
    --calls->size;
}

// The inlined Add: SetSize, then the record assigned (the text shared).
void AppendCall(AIHeardCalls* calls, const Vector3& point, int kind, std::uint32_t mask, const std::string& text) {
    int index = calls->size;
    calls->SetSize(index + 1, -1);
    AICallRecord& added = calls->data[index];
    added.point = point;
    added.kind = kind;
    added.hostileMask = static_cast<int>(mask);
    added.text = text;
}


} // namespace

void AIHeardCalls::Hear(const Vector3* point, int kind, const char* text) {
    for (int i = 0; i < size; ++i) {
        const AICallRecord& call = data[i];
        if (call.hostileMask == 0 && call.kind >= kind) RemoveCall(this, i--);
    }
    const Vector3 at = *point;
    std::string copy = text;
    AppendCall(this, at, kind, 0, copy);
}

void AIHeardCalls::AddClaim(std::uint32_t mask, int kind, const char* text) {
    std::string copy;
    copy = text;
    // The point is left as the stack held it (saved as memory.callN.point); 0,0,0 is the
    // canonical value, which the harness gives game.exe too (harness_save_leftovers.cpp).
    AppendCall(this, Vector3{0.0f, 0.0f, 0.0f}, kind, mask, copy);
}

void AIHeardCalls::RemoveClaims() {
    for (int i = 0; i < size; ++i)
        if (data[i].hostileMask != 0) RemoveCall(this, i--);
}

void AIMemory::HearCall(const Vector3* point, int kind, const char* text) { calls.Hear(point, kind, text); }
void AIMemory::AddClaim(std::uint32_t mask, int kind, const char* text) { calls.AddClaim(mask, kind, text); }

void AIViewer::Noise(CUnitServer* source, float loudness, int kind, const Vector3* at) {
    mfc::CTypedPtrList<CUnit> units{};
    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.blockSize = 10;
    CUnit* unit = source->unit;
    const Vector3* point = at ? at : &unit->localPosition_;
    map->SectorMap()->FindUnitsInRadius(point, &units, kNoiseRadius);
    for (auto* node = units.head; node;) {
        auto* other = node->data;
        node = node->next;
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
    units.RemoveAll();
}

AIEventPresence* AIViewer::StepNoise(const Vector3* position, CUnitServer* source) {
    constexpr int kStepNoise = 0x1A;
    const AITerrainType* ground = TheServerWorld()->AIMap()->MaterialRecordAt(position);
    float loudness = source->moveFactors[0] * source->stats.valuesE0[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, kStepNoise, nullptr);
    return nullptr;
}
