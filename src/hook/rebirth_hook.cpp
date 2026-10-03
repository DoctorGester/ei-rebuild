#include "rebirth_hook.h"

#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace ei::rebirth_hook {

using rebirth::Patch;

namespace {

enum Source : std::uint8_t {
    kAddon,      // addon.dll, applied before WinMain (seen by Detect)
    kEiPlugin,   // EI_Plugin.asi, applied later when the sound system loads it
};

// EI_Plugin sites that only some settings write (see EiPluginSiteActive).
enum Variant : std::uint8_t {
    kAlways,
    kNPlayersOver7,    // NPlayers > 7 only
    kPause12,          // Pause 1 or 2
    kPause3,           // Pause 3
    kNewMasterOrTest,  // NewMaster or Test
    kWithTest,         // the feature's site written only with Test as well
    kNever,            // written when the plugin unloads (game exit)
};

struct Site {
    Patch patch;
    std::uint32_t address;
    std::uint32_t size;
    Source source;
    Variant variant;
};

// Generated from config/rebirth_patches.csv by tools/gen_rebirth_sites.py.
constexpr Site kSites[] = {
#include "rebirth_sites.inc"
};
constexpr std::size_t kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

bool g_siteDiffers[kSiteCount];         // addon.dll wrote this site (it differs from the file)
bool g_applied[rebirth::kPatchCount];   // addon.dll: some site differs from game.exe on disk
bool g_plugin[rebirth::kPatchCount];    // EI_Plugin.asi will apply the feature
bool g_pluginPresent;
int g_pluginNPlayers;
int g_pluginPause;
bool g_pluginTest;
bool g_pluginNewMaster;
bool g_rebirthLaunch;

const unsigned char* Live(std::uint32_t address) { return reinterpret_cast<const unsigned char*>(address); }

bool Overlaps(const Site& s, std::uintptr_t address, std::uint32_t size) {
    return s.address < address + size && s.address + s.size > address;
}

bool SiteDiffers(const Site& s, const std::vector<unsigned char>& file) {
    std::uint32_t off = s.address - static_cast<std::uint32_t>(kImageBase);
    if (off + s.size > file.size()) return false;
    return std::memcmp(Live(s.address), file.data() + off, s.size) != 0;
}

bool EiPluginSiteActive(const Site& s) {
    if (s.source != kEiPlugin || !g_plugin[static_cast<int>(s.patch)]) return false;
    switch (s.variant) {
    case kAlways: return true;
    case kNPlayersOver7: return g_pluginNPlayers > 7;
    case kPause12: return g_pluginPause == 1 || g_pluginPause == 2;
    case kPause3: return g_pluginPause == 3;
    case kNewMasterOrTest: return g_pluginNewMaster || g_pluginTest;
    case kWithTest: return g_pluginTest;
    case kNever: return false;
    }
    return false;
}

void PathNextToDll(const char* name, char* out) {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&PathNextToDll), &self);
    GetModuleFileNameA(self, out, MAX_PATH);
    if (char* slash = std::strrchr(out, '\\')) strcpy_s(slash + 1, out + MAX_PATH - (slash + 1), name);
}

// What EI_Plugin.asi will do when Miles loads it from the working directory: its DllMain and
// first call read EI_Plugin.ini [CONFIG] (defaults as in the plugin) and addon's mode word at
// 0073AF10 (0xAADDxxxx: bits that switch features off; addon's SetEiPluginMode writes 0xAADD0080).
void DetectEiPlugin() {
    DWORD attributes = GetFileAttributesA("EI_Plugin.asi");
    g_pluginPresent = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    if (!g_pluginPresent) return;
    const char* ini = ".\\EI_Plugin.ini";
    auto get = [ini](const char* key, int def) { return static_cast<int>(GetPrivateProfileIntA("CONFIG", key, def, ini)); };
    int pk = get("PK", 0), nPlayers = get("NPlayers", 7), pause = get("Pause", 0), autoAim = get("AutoAim", 0);
    int graphGen = get("GraphGen", 0), test = get("Test", 0), newMaster = get("NewMaster", 1);
    int scripts = get("Scripts", 1), briefings = get("Briefings", 1), fixes = get("Fixes", 1);
    std::uint32_t mode = *reinterpret_cast<const std::uint32_t*>(0x0073AF10);
    if (mode == 0xAADDFFFFu) {   // the plugin leaves the game alone
        g_pluginPresent = false;
        return;
    }
    std::uint32_t off = (mode & 0xFFFF0000u) == 0xAADD0000u ? mode & 0xFFFFu : 0;
    if (off & 0x001) pk = 0;
    if (off & 0x002) nPlayers = 6;
    if (off & 0x004) pause = 0;
    if (off & 0x008) autoAim = 0;
    if (off & 0x010) graphGen = 0;
    if (off & 0x020) test = 0;
    if (off & 0x040) newMaster = 0;
    if (off & 0x080) scripts = 0;
    if (off & 0x100) briefings = 0;
    if (off & 0x200) fixes = 0;
    auto set = [](Patch p, bool on) { g_plugin[static_cast<int>(p)] = on; };
    set(Patch::EiPluginInit, true);
    set(Patch::EiPluginPK, pk != 0);
    set(Patch::EiPluginNPlayers, nPlayers != 6);
    set(Patch::EiPluginPause, pause != 0);
    set(Patch::EiPluginAutoAim, autoAim != 0);
    set(Patch::EiPluginGraphGen, graphGen != 0);
    set(Patch::EiPluginTest, test != 0);
    set(Patch::EiPluginNewMaster, newMaster != 0 || test != 0);
    set(Patch::EiPluginScripts, scripts != 0);
    set(Patch::EiPluginBriefings, briefings != 0);
    set(Patch::EiPluginFixes, fixes != 0);
    set(Patch::EiPluginUnload, false);
    g_pluginNPlayers = nPlayers;
    g_pluginPause = pause;
    g_pluginTest = test != 0;
    g_pluginNewMaster = newMaster != 0;
}

// rebirth_sites_live.txt: every known site with its bytes in memory at WinMain, and where a
// CALL/JMP written there leads (module + offset), to check the inventory against a real launch.
void DumpSites(const std::vector<unsigned char>& file) {
    char path[MAX_PATH];
    PathNextToDll("rebirth_sites_live.txt", path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    std::fprintf(f, "# patch site size source differs live-bytes [call/jmp target]\n");
    for (const Site& s : kSites) {
        bool differs = s.source == kAddon && SiteDiffers(s, file);
        std::fprintf(f, "%s %08X %u %s %d ", rebirth::Name(s.patch), s.address, s.size,
                     s.source == kAddon ? "addon" : "EI_Plugin", differs ? 1 : 0);
        for (std::uint32_t i = 0; i < s.size && i < 16; ++i) std::fprintf(f, "%02X", Live(s.address)[i]);
        if (s.size > 16) std::fprintf(f, "...");
        const unsigned char* p = Live(s.address);
        if (differs && (p[0] == 0xE8 || p[0] == 0xE9)) {
            std::int32_t rel;
            std::memcpy(&rel, p + 1, 4);
            std::uintptr_t target = s.address + 5 + rel;
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(target), &module)) {
                GetModuleFileNameA(module, name, MAX_PATH);
            }
            const char* base = std::strrchr(name, '\\');
            std::fprintf(f, " -> %08X %s+%X", static_cast<unsigned>(target), base ? base + 1 : name,
                         static_cast<unsigned>(target - reinterpret_cast<std::uintptr_t>(module)));
        }
        std::fputc('\n', f);
    }
    std::fclose(f);
}

void LogList(const char* label, const bool* set) {
    char line[2048];
    int n = std::snprintf(line, sizeof(line), "%s:", label);
    int count = 0;
    for (int i = 0; i < rebirth::kPatchCount && n > 0 && n < static_cast<int>(sizeof(line)) - 40; ++i) {
        if (!set[i]) continue;
        n += std::snprintf(line + n, sizeof(line) - n, " %s", rebirth::Name(static_cast<Patch>(i)));
        ++count;
    }
    if (count == 0) strcat_s(line, " none");
    Log("%s", line);
}

// The addon site covering `address`, preferring one addon wrote in this launch.
const Site* AddonSiteAt(std::uintptr_t address) {
    const Site* found = nullptr;
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        const Site& s = kSites[i];
        if (s.source != kAddon || s.address > address || address >= s.address + s.size) continue;
        if (g_siteDiffers[i]) return &s;
        if (!found) found = &s;
    }
    return found;
}

} // namespace

void Detect(const std::vector<unsigned char>& file) {
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        const Site& s = kSites[i];
        g_siteDiffers[i] = s.source == kAddon && SiteDiffers(s, file);
        if (g_siteDiffers[i]) g_applied[static_cast<int>(s.patch)] = true;
    }
    DetectEiPlugin();
    for (int i = 0; i < rebirth::kPatchCount; ++i) g_rebirthLaunch |= g_applied[i] || g_plugin[i];
    if (!g_rebirthLaunch) {
        Log("Rebirth: no addon.dll patch in memory and no EI_Plugin.asi: vanilla launch");
    } else {
        LogList("Rebirth: addon.dll applied", g_applied);
        if (g_pluginPresent) LogList("Rebirth: EI_Plugin.asi will apply", g_plugin);
        DumpSites(file);
        if (rebirth::FollowAddon()) {
            for (int i = 0; i < rebirth::kPatchCount; ++i) rebirth::Set(static_cast<Patch>(i), g_applied[i] || g_plugin[i]);
            Log("Rebirth: FollowAddon: every flag follows what the mods apply");
        }
    }
    bool enabled[rebirth::kPatchCount];
    for (int i = 0; i < rebirth::kPatchCount; ++i) enabled[i] = rebirth::Enabled(static_cast<Patch>(i));
    LogList("Rebirth flags on", enabled);
}

bool RebirthLaunch() { return g_rebirthLaunch; }

Verdict Check(std::uintptr_t address, std::uint32_t size, const unsigned char* orig, int stolen,
              char* why, std::size_t whySize) {
    why[0] = 0;
    bool patched = false;
    // EI_Plugin.asi writes after this point: into the original body we would no longer run.
    for (const Site& s : kSites) {
        if (!Overlaps(s, address, size) || !EiPluginSiteActive(s)) continue;
        if (!rebirth::ImplementsAt(address, s.patch) || !rebirth::Enabled(s.patch)) {
            std::snprintf(why, whySize, "EI_Plugin.asi patches +0x%X (%s) after WinMain",
                          static_cast<unsigned>(s.address - address), rebirth::Name(s.patch));
            return Verdict::Skip;
        }
        if (stolen > 0 && s.address < address + stolen) {
            std::snprintf(why, whySize, "EI_Plugin.asi patches the stolen bytes (%s)", rebirth::Name(s.patch));
            return Verdict::Skip;
        }
        patched = true;
    }
    const unsigned char* live = Live(static_cast<std::uint32_t>(address));
    for (std::uint32_t i = 0; i < size; ++i) {
        if (live[i] == orig[i]) continue;
        const Site* s = AddonSiteAt(address + i);
        if (!s) {
            std::snprintf(why, whySize, "byte +0x%X differs from game.exe on disk (patched by another mod)", i);
            return Verdict::Skip;
        }
        if (!rebirth::ImplementsAt(address, s->patch)) {
            std::snprintf(why, whySize, "Rebirth patch %s at +0x%X is not implemented by the rewrite",
                          rebirth::Name(s->patch), i);
            return Verdict::Skip;
        }
        if (!rebirth::Enabled(s->patch)) {
            std::snprintf(why, whySize, "Rebirth patch %s is applied but its flag is off", rebirth::Name(s->patch));
            return Verdict::Skip;
        }
        if (stolen > 0 && i < static_cast<std::uint32_t>(stolen) && s->address + s->size > address + stolen) {
            std::snprintf(why, whySize, "the %d stolen bytes end inside Rebirth patch %s's site", stolen,
                          rebirth::Name(s->patch));
            return Verdict::Skip;
        }
        patched = true;
    }
    if (!patched) return Verdict::Unpatched;
    int n = std::snprintf(why, whySize, "implements");
    for (const rebirth::Implements* d = rebirth::FirstImplements(); d; d = d->next) {
        if (d->address != address) continue;
        for (int k = 0; k < d->count && n > 0 && n < static_cast<int>(whySize) - 40; ++k)
            n += std::snprintf(why + n, whySize - n, " %s", rebirth::Name(d->patches[k]));
    }
    return Verdict::Implemented;
}

bool FlagWithoutOriginal(std::uintptr_t address, std::uint32_t size, const unsigned char* orig,
                         char* why, std::size_t whySize) {
    const unsigned char* live = Live(static_cast<std::uint32_t>(address));
    for (const rebirth::Implements* d = rebirth::FirstImplements(); d; d = d->next) {
        if (d->address != address) continue;
        for (int k = 0; k < d->count; ++k) {
            Patch p = d->patches[k];
            if (!rebirth::Enabled(p)) continue;
            bool originalHas = false;
            for (const Site& s : kSites) {
                if (s.patch != p || !Overlaps(s, address, size)) continue;
                if (s.source == kEiPlugin) {
                    originalHas |= EiPluginSiteActive(s);
                } else {
                    std::uint32_t from = s.address > address ? s.address - static_cast<std::uint32_t>(address) : 0;
                    std::uint32_t to = s.address + s.size - static_cast<std::uint32_t>(address);
                    if (to > size) to = size;
                    originalHas |= std::memcmp(live + from, orig + from, to - from) != 0;
                }
            }
            if (!originalHas) {
                std::snprintf(why, whySize, "%s", rebirth::Name(p));
                return true;
            }
        }
    }
    return false;
}

bool RelocateStolen(std::uintptr_t address, int stolen, unsigned char* trampoline) {
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        const Site& s = kSites[i];
        if (!g_siteDiffers[i] || s.address < address || s.address >= address + stolen) continue;
        const unsigned char* p = Live(s.address);
        std::uint32_t off = s.address - static_cast<std::uint32_t>(address);
        // Sites of 5 bytes or more start with an instruction addon wrote (JMP/CALL rel32 and NOPs, or
        // whole instructions); shorter ones may be operands (absolute, so they move as they are),
        // except a 2-byte short jump.
        if (s.size >= 5 && (p[0] == 0xE8 || p[0] == 0xE9)) {
            std::int32_t rel;
            std::memcpy(&rel, p + 1, 4);
            std::uintptr_t target = s.address + 5 + rel;
            std::int32_t moved = static_cast<std::int32_t>(target - (reinterpret_cast<std::uintptr_t>(trampoline) + off + 5));
            std::memcpy(trampoline + off + 1, &moved, 4);
        } else if ((s.size >= 5 && p[0] == 0x0F && p[1] >= 0x80 && p[1] <= 0x8F) ||
                   (s.size == 2 && (p[0] == 0xEB || (p[0] >= 0x70 && p[0] <= 0x7F)))) {
            return false;   // a relative branch addon wrote: would land elsewhere from the trampoline
        }
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, static_cast<SIZE_T>(stolen) + 5);
    return true;
}

} // namespace ei::rebirth_hook
