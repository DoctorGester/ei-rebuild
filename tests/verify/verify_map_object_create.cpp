// Checks for map_object_create.cpp: the Create slots of units (0052E400) and torches (0052BFC0)
// allocate (the network fields) and reset the object, so only ours runs. Afterwards: result 1,
// the DeltaState at +0x1C4 holds exactly 14 (units) / 5 (torches) fields with a buffer at least
// that large, its mode is idle (4) and, for a unit, the carried array is empty (Reset); a torch's
// +0x238 is null. Units: every unit made (the zone loader, AddUnitToServer; newgame). Torches:
// zones with torches (class 0x3A).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/unit.h"

#include <cstdint>

namespace {
void* s_unusedUnit;
void* s_unusedTorch;

const DeltaState* NetState(const void* object) {
    return reinterpret_cast<const DeltaState*>(static_cast<const std::uint8_t*>(object) + 0x1C4);
}
int Capacity(const DeltaState* state) { return static_cast<int>(state->unknown_C); }

const char* CheckState(const DeltaState* state, int fields) {
    if (state->fieldCount_ != fields) return "field count";
    if (!state->fields_ || Capacity(state) < fields) return "field buffer";
    if (state->mode_ != DeltaState::kIdle) return "mode";
    return nullptr;
}
} // namespace

static int __fastcall CreateUnitVerify(CUnit* unit) {
    int result = unit->CreateUnit();
    const char* problem = result != 1 ? "result" : CheckState(NetState(unit), 14);
    if (!problem && unit->carriedCount != 0) problem = "carried items";
    ei::verify::Check("CUnit::CreateUnit", true, problem == nullptr, "unit %d: %s (fields %d)", unit->id_,
                      problem ? problem : "", NetState(unit)->fieldCount_);
    return result;
}
EI_VERIFY(0x0052E400, CreateUnitVerify, 0, s_unusedUnit);

static int __fastcall TorchCreateVerify(CMapObject* torch) {
    int result = TorchCreate(torch);
    const char* problem = result != 1 ? "result" : CheckState(NetState(torch), 5);
    if (!problem && *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(torch) + 0x238)) problem = "+0x238";
    ei::verify::Check("TorchCreate", true, problem == nullptr, "object %d: %s (fields %d)", torch->id_,
                      problem ? problem : "", NetState(torch)->fieldCount_);
    return result;
}
EI_VERIFY(0x0052BFC0, TorchCreateVerify, 0, s_unusedTorch);
