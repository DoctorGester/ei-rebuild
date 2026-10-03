// Check for the unit record (unit_record.cpp, 00477B00).
//
// The function creates items and spells and gives them to the unit through code that is not
// rewritten, so ours runs alone. Equipping recomputes the unit's current values, so of the fields
// the record sets only those nothing later in the function recomputes are checked: the attribute
// bytes (controller +0x8A8) and the seven bytes at controller +0x270.
#include "verify.h"

#include "../../src/game/unit_record.h"

#include <cstdio>
#include <cstring>

namespace {

using namespace unitrecord;

std::uint32_t Dword(const char* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, p + offset, 4);
    return v;
}

bool HasStats(const char* r) {
    float zero = 0.0f, f10, f14;
    std::memcpy(&f10, r + field::kNoStats0, 4);
    std::memcpy(&f14, r + field::kNoStats2, 4);
    bool eq10 = !(f10 < zero) && !(f10 > zero);
    bool eq14 = !(f14 < zero) && !(f14 > zero);
    bool noStats = eq10 && Dword(r, field::kNoStats1) == 0 && eq14;
    return !noStats && Dword(r, field::kApplyStats) != 0;
}

void* s_unused;
void __fastcall ApplyToVerify(UnitRecord* self, void*, CUnit* unit) {
    const char* r = reinterpret_cast<const char*>(self);
    bool stats = HasStats(r);
    self->ApplyTo(unit);
    if (!stats) {
        ei::verify::Check("UnitRecord::ApplyTo (items only)", true, true, "");
        return;
    }
    const char* u = reinterpret_cast<const char*>(unit);
    const char* c = *reinterpret_cast<const char* const*>(u + 0x240);
    char detail[160] = "";
    if (std::memcmp(c + 0x270, r + field::kBytes54, 7) != 0 || std::memcmp(c + 0x8A8, r + field::kBytesA4, 3) != 0)
        std::snprintf(detail, sizeof(detail), "byte blocks +0x270/+0x8A8");
    ei::verify::Check("UnitRecord::ApplyTo", true, detail[0] == 0, "%s", detail);
}

} // namespace

EI_VERIFY(0x00477B00, ApplyToVerify, 0, s_unused);
