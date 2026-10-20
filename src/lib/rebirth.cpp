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
#define EI_REBIRTH_PATCH(name, kind) #name,
#include "ei/rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kPatchCount);

const Kind kKinds[] = {
#define EI_REBIRTH_PATCH(name, kind) Kind::kind,
#include "ei/rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == kPatchCount);

int g_pluginNPlayers = 7;
int g_pluginPause = 1;
char g_secondMaster[256];
char g_queued[1024];                         // QueueSpec runs before any C++ object may be used

void SetAll(bool on) {
    for (bool& e : detail::g_enabled) e = on;
}

void SetDefaults() {
    for (int i = 0; i < kPatchCount; ++i) detail::g_enabled[i] = kKinds[i] == Kind::Fix;
}
} // namespace

void Set(Patch p, bool on) { detail::g_enabled[static_cast<int>(p)] = on; }

Kind KindOf(Patch p) { return kKinds[static_cast<int>(p)]; }

int PluginNPlayers() { return g_pluginNPlayers; }
void SetPluginNPlayers(int players) { g_pluginNPlayers = players; }

int PluginPause() { return g_pluginPause; }
void SetPluginPause(int mode) { g_pluginPause = mode; }

const char* PluginSecondMaster() { return g_secondMaster; }
void SetPluginSecondMaster(const char* address) { strncpy_s(g_secondMaster, address, _TRUNCATE); }

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
        if (_strnicmp(token, "SecondMaster=", 13) == 0) {
            SetPluginSecondMaster(token + 13);
            continue;
        }
        bool on = true;
        if (char* eq = std::strchr(token, '=')) {
            on = eq[1] != '0';
            *eq = 0;
        }
        Patch patch;
        if (_stricmp(token, "all") == 0) SetAll(on);
        else if (_stricmp(token, "none") == 0) SetAll(!on);
        else if (_stricmp(token, "defaults") == 0) SetDefaults();
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
    const int all = static_cast<int>(GetPrivateProfileIntA("Rebirth", "Default", -1, iniPath));
    if (all >= 0) SetAll(all != 0);
    else SetDefaults();
    for (int i = 0; i < kPatchCount; ++i) {
        int value = static_cast<int>(GetPrivateProfileIntA("Rebirth", kNames[i], -1, iniPath));
        if (value >= 0) detail::g_enabled[i] = value != 0;
    }
    GetPrivateProfileStringA("Rebirth", "SecondMaster", "", g_secondMaster, sizeof(g_secondMaster), iniPath);
    char env[1024];
    DWORD n = GetEnvironmentVariableA("EI_REBIRTH", env, sizeof(env));
    if (n > 0 && n < sizeof(env)) ApplySpec(env);
    ApplySpec(g_queued);
}

} // namespace ei::rebirth
