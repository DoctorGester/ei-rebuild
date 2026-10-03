// Differential checks for the options loader (options.cpp, 0045B210) and writer (0045C560).
//
// The loader only reads the registry (opening the key creates it when missing, as the original
// does), so both can run: the original on a copy of the options object, ours on the real one. The
// copy's CString fields are copy-constructed (MFC strings are reference counted, so a byte copy
// would let the original's assignments release the real object's strings) and destroyed after the
// comparison. Every byte of the loaded range must match, except the strings, whose text must.
#include "verify.h"

#include "../../src/game/options.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

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

// 0045C560 writes the options through the registry key helpers, which call RegCreateKeyExA /
// RegSetValueExA through the import table. The wrapper points both slots at recorders: the original
// runs first and its writes are only recorded (not passed on, so every value is written once), then
// ours runs with the writes passed on to whatever the slots held before (under the harness, a stub
// that skips them). The two event lists (subkey opened; value name, type and data) must match.
constexpr std::uintptr_t kRegCreateKeyExASlot = 0x0073B010;
constexpr std::uintptr_t kRegSetValueExASlot = 0x0073B018;

decltype(&::RegCreateKeyExA) s_createKey;
decltype(&::RegSetValueExA) s_setValue;
std::vector<std::string>* s_events;
bool s_passOn;

LONG WINAPI RecordCreateKey(HKEY root, LPCSTR subkey, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam,
                            LPSECURITY_ATTRIBUTES sa, PHKEY result, LPDWORD disposition) {
    s_events->push_back(std::string("key ") + (subkey ? subkey : "(null)"));
    return s_createKey(root, subkey, reserved, cls, options, sam, sa, result, disposition);
}

LONG WINAPI RecordSetValue(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size) {
    char head[96];
    std::snprintf(head, sizeof(head), "value %s type %lu size %lu:", name ? name : "(default)", type, size);
    std::string e(head);
    for (DWORD i = 0; data && i < size; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), " %02X", data[i]);
        e += b;
    }
    s_events->push_back(e);
    return s_passOn ? s_setValue(key, name, reserved, type, data, size) : ERROR_SUCCESS;
}

template <class T>
T SwapSlot(std::uintptr_t address, T replacement) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    T previous = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return previous;
}

// 0045C560: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_save)(GameOptions*, void*, const char*);
void __fastcall SaveToRegistryVerify(GameOptions* self, void*, const char* path) {
    std::vector<std::string> original, ours;
    s_createKey = SwapSlot(kRegCreateKeyExASlot, &RecordCreateKey);
    s_setValue = SwapSlot(kRegSetValueExASlot, &RecordSetValue);

    s_events = &original;
    s_passOn = false;
    s_save(self, nullptr, path);
    s_events = &ours;
    s_passOn = true;
    self->SaveToRegistry(path);

    SwapSlot(kRegCreateKeyExASlot, s_createKey);
    SwapSlot(kRegSetValueExASlot, s_setValue);
    s_events = nullptr;

    char detail[200] = "";
    std::size_t n = original.size() < ours.size() ? original.size() : ours.size();
    for (std::size_t i = 0; i < n && !detail[0]; ++i) {
        if (original[i] != ours[i])
            std::snprintf(detail, sizeof(detail), "event %u: [%.80s] vs [%.80s]", unsigned(i), original[i].c_str(), ours[i].c_str());
    }
    if (!detail[0] && original.size() != ours.size())
        std::snprintf(detail, sizeof(detail), "%u events vs %u", unsigned(original.size()), unsigned(ours.size()));
    ei::verify::Check("GameOptions::SaveToRegistry", true, detail[0] == 0, "%s", detail);
}

} // namespace

EI_VERIFY(0x0045B210, LoadFromRegistryVerify, 7, s_load);
EI_VERIFY(0x0045C560, SaveToRegistryVerify, 7, s_save);
