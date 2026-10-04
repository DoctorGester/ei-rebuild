// Checks for options_defaults.cpp: the original sets the defaults first; its bytes (the 0x26C the
// registry code uses) and the texts of the CString fields are recorded; then ours sets them again
// and both are compared.
#include "verify.h"

#include "../../src/game/options.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace options {
void __fastcall OptionsResetDefaults(GameOptions* self);
}

namespace {
using namespace options;

bool IsString(std::size_t offset) {
    for (std::size_t s : kStringFields)
        if (s == offset) return true;
    return false;
}

// 00458DA0: PUSH EBX, PUSH EBP, PUSH ESI, PUSH EDI (4), MOV ESI,ECX (2).
void(__fastcall* s_reset)(GameOptions*);
void __fastcall ResetVerify(GameOptions* self) {
    s_reset(self);
    auto* p = reinterpret_cast<std::uint8_t*>(self);
    std::uint8_t expected[field::kLoadedSize];
    std::memcpy(expected, p, sizeof(expected));
    std::string texts[sizeof(kStringFields) / sizeof(kStringFields[0])];
    for (std::size_t i = 0; i < sizeof(kStringFields) / sizeof(kStringFields[0]); ++i)
        texts[i] = *reinterpret_cast<const char* const*>(p + kStringFields[i]);
    OptionsResetDefaults(self);
    bool ok = true;
    std::size_t bad = 0;
    for (std::size_t o = 0; ok && o < field::kLoadedSize; o += 4) {
        if (IsString(o)) continue;
        if (std::memcmp(expected + o, p + o, 4) != 0) {
            ok = false;
            bad = o;
        }
    }
    for (std::size_t i = 0; ok && i < sizeof(kStringFields) / sizeof(kStringFields[0]); ++i) {
        if (texts[i] != *reinterpret_cast<const char* const*>(p + kStringFields[i])) {
            ok = false;
            bad = kStringFields[i];
        }
    }
    ei::verify::Check("OptionsResetDefaults", true, ok, std::to_string(bad).c_str());
}
} // namespace

EI_VERIFY(0x00458DA0, ResetVerify, 6, s_reset);
