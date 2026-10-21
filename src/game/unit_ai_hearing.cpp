// What the AI hears: the heard calls' Hear / AddClaim / RemoveClaims (005CCD40, 005CCEA0,
// 005CCBF0), the perception memory's two entry points (005CBBA0, 005CBBC0) and the AI events'
// Noise (005C98C0). See unit_ai.h and docs/notes/unit_ai.md.
#include "unit_ai.h"
#include "combat.h"

#include "ai_map.h"
#include "console.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

namespace {
// The CPtrList the sector map fills (vtable 0073D454, blocks of 10).
void* const kPtrListVtable = ei::GamePointer<void>(0x0073D454);
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
constexpr int kAlertedLevel = 900;      // a unit noticing something at this level or above hears nothing
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";   // 007919FC

mfc::CString& TextOf(AICallRecord& call) { return *reinterpret_cast<mfc::CString*>(&call.text); }

// The inlined RemoveAt(index, 1): the record's text freed, the tail moved down.
void RemoveCall(AICallArray* calls, int index) {
    calls->data[index].Destruct();
    int after = calls->size - index - 1;
    if (after != 0) libcrt::Memmove(&calls->data[index], &calls->data[index + 1], static_cast<std::size_t>(after) * sizeof(AICallRecord));
    --calls->size;
}

// The inlined Add: SetSize through its address, then the record assigned (the text shared).
void AppendCall(AICallArray* calls, const Vector3& point, int kind, std::uint32_t mask, const mfc::CString& text) {
    int index = calls->size;
    calls->SetSize(index + 1, -1);
    AICallRecord& added = calls->data[index];
    added.point = point;
    added.kind = kind;
    added.hostileMask = static_cast<int>(mask);
    TextOf(added) = text;
}

CUnitServer* ControllerOf(const CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }
AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

} // namespace

void __fastcall CallsHear(AICallArray* calls, void*, const Vector3* point, int kind, const char* text) {
    for (int i = 0; i < calls->size; ++i) {
        const AICallRecord& call = calls->data[i];
        if (call.hostileMask == 0 && call.kind >= kind) RemoveCall(calls, i--);
    }
    const Vector3 at = *point;
    mfc::CString copy(text);
    AppendCall(calls, at, kind, 0, copy);
}
EI_REPLACE(0x005CCD40, CallsHear);

void __fastcall CallsAddClaim(AICallArray* calls, void*, std::uint32_t mask, int kind, const char* text) {
    mfc::CString copy;
    copy = text;
    // The original leaves the point as its stack held it (saved as memory.callN.point); 0,0,0 is the
    // canonical value, which the harness gives the original too (harness_save_leftovers.cpp, S20d).
    AppendCall(calls, Vector3{0.0f, 0.0f, 0.0f}, kind, mask, copy);
}
EI_REPLACE(0x005CCEA0, CallsAddClaim);

void __fastcall CallsRemoveClaims(AICallArray* calls) {
    for (int i = 0; i < calls->size; ++i)
        if (calls->data[i].hostileMask != 0) RemoveCall(calls, i--);
}
EI_REPLACE(0x005CCBF0, CallsRemoveClaims);

// 005CBBA0 / 005CBBC0 (RET 0xC, thiscall on the memory, brain + 0xD0): its calls (+0xA8) hear.
void __fastcall MemoryHearCall(std::uint8_t* memory, void*, const Vector3* point, int kind, const char* text) {
    CallsHear(&BrainOfMemory(memory)->calls, nullptr, point, kind, text);
}
EI_REPLACE(0x005CBBA0, MemoryHearCall);
void __fastcall MemoryAddClaim(std::uint8_t* memory, void*, std::uint32_t mask, int kind, const char* text) {
    CallsAddClaim(&BrainOfMemory(memory)->calls, nullptr, mask, kind, text);
}
EI_REPLACE(0x005CBBC0, MemoryAddClaim);

void AIViewer::Noise(CUnitServer* source, float loudness, int kind, const Vector3* at) {
    mfc::CTypedPtrList<CEngineObject> units{};
    units.vtable = kPtrListVtable;
    units.blockSize = 10;
    CUnit* unit = source->unit;
    const Vector3* point = at ? at : &unit->position;
    map->SectorMap()->FindUnitsInRadius(point, &units, kNoiseRadius);
    for (auto* node = units.head; node;) {
        auto* other = static_cast<CUnit*>(node->data);
        node = node->next;
        if (!other->IsHostileTo(unit)) continue;
        CUnitServer* control = ControllerOf(other);
        AIBrain* brain = control->Brain();
        if (brain->noticedLevel >= kAlertedLevel) continue;
        const double hearing = static_cast<double>(control->moveFactors[1]) * control->stats.valuesB0[3].current;
        const float distance = vecmath::Distance<Sum3::XYZ>(*point, other->position);
        if (!(hearing * loudness > distance)) continue;
        mfc::CString text;
        if (console::FilterMask() & 1) {
            const double between = vecmath::Distance<Sum3::XYZ>(other->position, unit->position);
            text.Format(kHeardFormat, other->id_, unit->id_, between);
        }
        CallsHear(&brain->calls, nullptr, &unit->position, kind, text);
    }
    // The list's RemoveAll: no element destruction.
    units.RemoveAll();
}
EI_REPLACE_METHOD(0x005C98C0, AIViewer::Noise);

int AIViewer::StepNoise(const Vector3* position, CUnitServer* source) {
    constexpr int kStepNoise = 0x1A;
    auto* ground = static_cast<const AITerrainType*>(WorldServer()->AIMap()->MaterialRecordAt(position));
    float loudness = source->moveFactors[0] * source->stats.valuesE0[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, kStepNoise, nullptr);
    return 0;
}
EI_REPLACE_METHOD(0x005C9770, AIViewer::StepNoise);
