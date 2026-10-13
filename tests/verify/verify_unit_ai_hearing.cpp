// Checks for what the AI hears (src/game/unit_ai_hearing.cpp). Hear / AddClaim / RemoveClaims run
// ours only (they free and share CString texts) against a model of the array taken before. Hear is
// reached by CallAllies and every Noise (combat scenarios: an attack or skill use is a noise of
// kind 0x1A; unit_ai, combat_more), AddClaim and RemoveClaims by the alarm and attack code.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/originals_ai.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct CallModel {
    Vector3 point;
    int kind;
    int mask;
    std::string text;
};
std::vector<CallModel> Snapshot(const AICallArray* calls) {
    std::vector<CallModel> out;
    for (int i = 0; i < calls->size; ++i) {
        const AICallRecord& c = calls->data[i];
        out.push_back({c.point, c.kind, c.unknown_10, c.text ? c.text : ""});
    }
    return out;
}
// `skipLastPoint`: AddClaim leaves the new record's point unset in the original.
bool Matches(const AICallArray* calls, const std::vector<CallModel>& expected, bool skipLastPoint) {
    if (calls->size != static_cast<int>(expected.size())) return false;
    for (int i = 0; i < calls->size; ++i) {
        const AICallRecord& c = calls->data[i];
        const CallModel& e = expected[i];
        bool last = i + 1 == calls->size;
        if (!(skipLastPoint && last) && std::memcmp(&c.point, &e.point, sizeof(Vector3)) != 0) return false;
        if (c.kind != e.kind || c.unknown_10 != e.mask || e.text != (c.text ? c.text : "")) return false;
    }
    return true;
}
AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

void* s_unusedHear;
void* s_unusedClaim;
void* s_unusedRemoveClaims;
void* s_unusedMemoryHear;
void* s_unusedMemoryClaim;
void* s_unusedNoise;
} // namespace

// 005CCD40: unflagged calls of `kind` or above dropped, then the new one appended.
static void __fastcall HearVerify(AICallArray* self, void*, const Vector3* point, int kind, const char* text) {
    std::vector<CallModel> expected;
    for (const CallModel& c : Snapshot(self))
        if (c.mask != 0 || c.kind < kind) expected.push_back(c);
    expected.push_back({*point, kind, 0, text ? text : ""});
    CallsHear(self, nullptr, point, kind, text);
    ei::verify::Check("AICallArray::Hear", true, Matches(self, expected, false), "array %p kind %d: %d calls (expected %d)",
                      self, kind, self->size, static_cast<int>(expected.size()));
}
EI_VERIFY(0x005CCD40, HearVerify, 0, s_unusedHear);

// 005CCEA0: one record appended (its point is not compared).
static void __fastcall AddClaimVerify(AICallArray* self, void*, std::uint32_t mask, int kind, const char* text) {
    std::vector<CallModel> expected = Snapshot(self);
    expected.push_back({Vector3{}, kind, static_cast<int>(mask), text ? text : ""});
    CallsAddClaim(self, nullptr, mask, kind, text);
    ei::verify::Check("AICallArray::AddClaim", true, Matches(self, expected, true), "array %p mask %08X kind %d", self,
                      mask, kind);
}
EI_VERIFY(0x005CCEA0, AddClaimVerify, 0, s_unusedClaim);

// 005CCBF0: the masked calls dropped, the others kept in order.
static void __fastcall RemoveClaimsVerify(AICallArray* self) {
    std::vector<CallModel> expected;
    for (const CallModel& c : Snapshot(self))
        if (c.mask == 0) expected.push_back(c);
    CallsRemoveClaims(self);
    ei::verify::Check("AICallArray::RemoveClaims", true, Matches(self, expected, false), "array %p: %d calls left", self,
                      self->size);
}
EI_VERIFY(0x005CCBF0, RemoveClaimsVerify, 0, s_unusedRemoveClaims);

// 005CBBA0 / 005CBBC0: ours only (they call the two above through their addresses, whose wrappers
// check the arrays); the last call is the one asked for.
void __fastcall MemoryHearCall(std::uint8_t* memory, void*, const Vector3* point, int kind, const char* text);
void __fastcall MemoryAddClaim(std::uint8_t* memory, void*, std::uint32_t mask, int kind, const char* text);
static void __fastcall MemoryHearVerify(std::uint8_t* memory, void*, const Vector3* point, int kind, const char* text) {
    MemoryHearCall(memory, nullptr, point, kind, text);
    const AICallArray& calls = BrainOfMemory(memory)->calls;
    ei::verify::Check("AIPerception::HearCall", true, calls.size > 0 && calls.data[calls.size - 1].kind == kind,
                      "memory %p kind %d", memory, kind);
}
EI_VERIFY(0x005CBBA0, MemoryHearVerify, 0, s_unusedMemoryHear);
static void __fastcall MemoryClaimVerify(std::uint8_t* memory, void*, std::uint32_t mask, int kind, const char* text) {
    MemoryAddClaim(memory, nullptr, mask, kind, text);
    const AICallArray& calls = BrainOfMemory(memory)->calls;
    ei::verify::Check("AIPerception::AddClaim", true,
                      calls.size > 0 && calls.data[calls.size - 1].unknown_10 == static_cast<int>(mask), "memory %p mask %08X",
                      memory, mask);
}
EI_VERIFY(0x005CBBC0, MemoryClaimVerify, 0, s_unusedMemoryClaim);

// 005C98C0 AIViewer::Noise: ours only. Afterwards the units within 50 of the point are asked
// again; each one that should have heard it (hostile to the source's unit, noticed level below
// 900, hearing x loudness beyond the distance: Noise changes none of that) ends its calls with
// {the source unit's position, kind, 0}.
static void __fastcall NoiseVerify(AIViewer* self, void*, UnitController* source, float loudness, int kind,
                                   const Vector3* at) {
    self->Noise(source, loudness, kind, at);
    CUnit* unit = source->unit;
    const Vector3* point = at ? at : &unit->position;
    mfc::CTypedPtrList<CEngineObject> units{};
    units.vtable = reinterpret_cast<void*>(0x0073D454);
    units.blockSize = 10;
    SectorMapFindUnitsInRadius(self->map->SectorMap(), nullptr, point, &units, 50.0f);
    int heard = 0, wrong = 0;
    for (auto* node = units.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (!other->IsHostileTo(unit)) continue;
        auto* control = reinterpret_cast<UnitController*>(other->controller);
        AIBrain* brain = control->Brain();
        if (brain->noticedLevel >= 900) continue;
        double hearing = static_cast<double>(control->moveFactors[1]) * control->stats.valuesB0[3].current;
        if (!(hearing * loudness > vecmath::Distance<Sum3::XYZ>(*point, other->position))) continue;
        ++heard;
        const AICallArray& calls = brain->calls;
        const AICallRecord* last = calls.size ? &calls.data[calls.size - 1] : nullptr;
        if (!last || last->kind != kind || last->unknown_10 != 0 ||
            std::memcmp(&last->point, &unit->position, sizeof(Vector3)) != 0)
            ++wrong;
    }
    units.RemoveAll();
    ei::verify::Check("AIViewer::Noise", 0, wrong, "source unit %d kind %d: %d of %d hearers without the call", unit->id_,
                      kind, wrong, heard);
}
EI_VERIFY(0x005C98C0, NoiseVerify, 0, s_unusedNoise);

// 005C9770 AIViewer::StepNoise (A-W14): ours only (Noise adds calls to the hearers' memories; its
// own wrapper above checks them); the result is 0 as the original's. Reached from the unit
// controller's update (CUnitServer::UpdateServer, every few frames for each moving unit).
static int __fastcall StepNoiseVerify(AIViewer* self, void*, const Vector3* position, UnitController* source) {
    int result = self->StepNoise(position, source);
    ei::verify::Check("AIViewer::StepNoise", 0, result, "source %p", source);
    return result;
}
EI_VERIFY(0x005C9770, StepNoiseVerify, 0, s_unusedNoise);
