// Differential check for the options loader (options.cpp, 0045B210).
//
// The loader only reads the registry (opening the key creates it when missing, as the original
// does), so both can run: the original on a copy of the options object, ours on the real one. The
// copy's CString fields are copy-constructed (MFC strings are reference counted, so a byte copy
// would let the original's assignments release the real object's strings) and destroyed after the
// comparison. Every byte of the loaded range must match, except the strings, whose text must.
#include "verify.h"

#include "../../src/game/options.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace {

using namespace options;

bool IsStringField(std::size_t offset) {
    for (std::size_t s : kStringFields)
        if (offset >= s && offset < s + 4) return true;
    return false;
}

// 0045B210: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_load)(GameOptions*, void*, const char*);
void __fastcall LoadFromRegistryVerify(GameOptions* self, void*, const char* path) {
    alignas(4) static unsigned char copy[field::kLoadedSize];
    std::memcpy(copy, self, sizeof(copy));
    for (std::size_t s : kStringFields)
        new (copy + s) mfc::CString(*reinterpret_cast<const mfc::CString*>(reinterpret_cast<const char*>(self) + s));

    s_load(reinterpret_cast<GameOptions*>(copy), nullptr, path);
    self->LoadFromRegistry(path);

    const unsigned char* real = reinterpret_cast<const unsigned char*>(self);
    char detail[160] = "";
    for (std::size_t i = 0; i < sizeof(copy) && !detail[0]; ++i) {
        if (!IsStringField(i) && copy[i] != real[i])
            std::snprintf(detail, sizeof(detail), "+0x%X: %02X vs %02X", unsigned(i), copy[i], real[i]);
    }
    for (std::size_t s : kStringFields) {
        const char* a = *reinterpret_cast<const char* const*>(copy + s);
        const char* b = *reinterpret_cast<const char* const*>(real + s);
        if (!detail[0] && std::strcmp(a, b) != 0) std::snprintf(detail, sizeof(detail), "+0x%X: \"%.40s\" vs \"%.40s\"", unsigned(s), a, b);
        reinterpret_cast<mfc::CString*>(copy + s)->~CString();
    }
    ei::verify::Check("GameOptions::LoadFromRegistry", true, detail[0] == 0, "%s", detail);
}

} // namespace

EI_VERIFY(0x0045B210, LoadFromRegistryVerify, 7, s_load);
