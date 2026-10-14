// Flags for Rebirth's patches (include/ei/rebirth.h).
#include "ei/rebirth.h"

#include <windows.h>

#include <cstring>

namespace ei::rebirth {

namespace detail {
bool g_enabled[kPatchCount];   // zero-initialised: every patch off until LoadSettings
}

namespace {
const char* const kNames[] = {
#define EI_REBIRTH_PATCH(name) #name,
#include "ei/rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kPatchCount);

bool g_followAddon = true;
char g_queued[1024];                         // QueueSpec runs before any C++ object may be used
const Implements* g_implements = nullptr;    // constant-initialised: registrations come first
const LiveStolen* g_liveStolen = nullptr;

void SetAll(bool on) {
    for (bool& e : detail::g_enabled) e = on;
}
} // namespace

void Set(Patch p, bool on) { detail::g_enabled[static_cast<int>(p)] = on; }

const char* Name(Patch p) { return kNames[static_cast<int>(p)]; }

bool FromName(const char* name, Patch* out) {
    for (int i = 0; i < kPatchCount; ++i) {
        if (_stricmp(kNames[i], name) == 0) {
            *out = static_cast<Patch>(i);
            return true;
        }
    }
    return false;
}

bool ApplySpec(const char* spec) {
    bool ok = true;
    const char* p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == ';' || *p == '\t') ++p;
        if (!*p) break;
        char token[96];
        std::size_t n = 0;
        while (*p && *p != ',' && *p != ' ' && *p != ';' && *p != '\t') {
            if (n + 1 < sizeof(token)) token[n++] = *p;
            ++p;
        }
        token[n] = 0;
        bool on = true;
        if (char* eq = std::strchr(token, '=')) {
            on = eq[1] != '0';
            *eq = 0;
        }
        Patch patch;
        if (_stricmp(token, "all") == 0) SetAll(on);
        else if (_stricmp(token, "none") == 0) SetAll(!on);
        else if (_stricmp(token, "FollowAddon") == 0) g_followAddon = on;
        else if (FromName(token, &patch)) Set(patch, on);
        else ok = false;
    }
    return ok;
}

void QueueSpec(const char* spec) {
    strcat_s(g_queued, spec);
    strcat_s(g_queued, ";");
}

void LoadSettings(const char* iniPath) {
    SetAll(GetPrivateProfileIntA("Rebirth", "Default", 0, iniPath) != 0);
    for (int i = 0; i < kPatchCount; ++i) {
        int value = static_cast<int>(GetPrivateProfileIntA("Rebirth", kNames[i], -1, iniPath));
        if (value >= 0) detail::g_enabled[i] = value != 0;
    }
    g_followAddon = GetPrivateProfileIntA("Rebirth", "FollowAddon", 1, iniPath) != 0;
    char env[1024];
    DWORD n = GetEnvironmentVariableA("EI_REBIRTH", env, sizeof(env));
    if (n > 0 && n < sizeof(env)) ApplySpec(env);
    ApplySpec(g_queued);
}

bool FollowAddon() { return g_followAddon; }

Implements::Implements(std::uintptr_t address_, const Patch* patches_, int count_)
    : address(address_), patches(patches_), count(count_), next(g_implements) {
    g_implements = this;
}

const Implements* FirstImplements() { return g_implements; }

LiveStolen::LiveStolen(std::uintptr_t address_) : address(address_), next(g_liveStolen) { g_liveStolen = this; }

bool LiveStolenAt(std::uintptr_t address) {
    for (const LiveStolen* l = g_liveStolen; l; l = l->next)
        if (l->address == address) return true;
    return false;
}

bool ImplementsAt(std::uintptr_t address, Patch p) {
    for (const Implements* i = g_implements; i; i = i->next) {
        if (i->address != address) continue;
        for (int k = 0; k < i->count; ++k)
            if (i->patches[k] == p) return true;
    }
    return false;
}

} // namespace ei::rebirth
