// The original game with Rebirth's bug fixes only: addon.dll's patches outside the run's Rebirth flags undone
// at WinMain, the rest checked against game.exe on disk. See harness_addon.h.
#include "harness_addon.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "../../src/game/game_data.h"
#include "ei/rebirth.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace ei::verify::addon {
namespace {

using ei::Log;
using rebirth::Patch;

struct Site {
    Patch patch;
    std::uintptr_t address;
    std::size_t size;
};
constexpr Site kSites[] = {
#include "addon_sites.inc"
};

std::string g_failure;

void Fail(const std::string& what) {
    Log("addon: %s", what.c_str());
    if (!g_failure.empty()) g_failure += "; ";
    g_failure += what;
}

bool ReadFile(const char* path, std::vector<unsigned char>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    out.resize(static_cast<std::size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    const bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

const IMAGE_NT_HEADERS32* Headers() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(kImageBase);
    return reinterpret_cast<const IMAGE_NT_HEADERS32*>(kImageBase + dos->e_lfanew);
}

bool InImportSlots(std::uintptr_t address) {
    const IMAGE_DATA_DIRECTORY& iat = Headers()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    return address >= kImageBase + iat.VirtualAddress && address < kImageBase + iat.VirtualAddress + iat.Size;
}

// What the loader put into the import slot `slot`: the import its name table names, looked up again.
std::uint32_t ImportAddress(std::uintptr_t slot) {
    const IMAGE_DATA_DIRECTORY& dir = Headers()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(kImageBase + dir.VirtualAddress); d->Name; ++d) {
        const auto* names = reinterpret_cast<const std::uint32_t*>(kImageBase + d->OriginalFirstThunk);
        const std::uintptr_t first = kImageBase + d->FirstThunk;
        for (std::size_t i = 0; d->OriginalFirstThunk && names[i]; ++i) {
            if (first + 4 * i != slot) continue;
            HMODULE module = GetModuleHandleA(reinterpret_cast<const char*>(kImageBase + d->Name));
            const char* name = names[i] & IMAGE_ORDINAL_FLAG32
                                   ? reinterpret_cast<const char*>(static_cast<std::uintptr_t>(names[i] & 0xFFFF))
                                   : reinterpret_cast<const char*>(kImageBase + names[i] + 2);
            return module ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(GetProcAddress(module, name))) : 0;
        }
    }
    return 0;
}

// Writes game.exe's own bytes back over `site` (an import slot: the import's address).
bool Undo(const Site& site, const unsigned char* disk) {
    unsigned char bytes[256];
    if (site.size > sizeof(bytes)) return false;
    std::memcpy(bytes, disk, site.size);
    if (InImportSlots(site.address)) {
        const std::uint32_t import = ImportAddress(site.address);
        if (!import || site.size != 4) return false;
        std::memcpy(bytes, &import, 4);
    }
    auto* target = ei::GamePointer<unsigned char>(site.address);
    DWORD old;
    if (!VirtualProtect(target, site.size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(target, bytes, site.size);
    VirtualProtect(target, site.size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, site.size);
    return true;
}

// addon's own hook (addon.log "HookWinMain"): the CRT's CALL of WinMain, which is running now; it stays.
constexpr std::uintptr_t kAddonWinMainCall = 0x006EC301;

bool InKeptSite(std::uintptr_t address) {
    if (address >= kAddonWinMainCall && address < kAddonWinMainCall + 5) return true;
    for (const Site& site : kSites)
        if (rebirth::Enabled(site.patch) && address >= site.address && address < site.address + site.size) return true;
    return false;
}

void LoadFlags() {
    char ini[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadFlags), &self);
    GetModuleFileNameA(self, ini, MAX_PATH);
    if (char* slash = std::strrchr(ini, '\\')) strcpy_s(slash + 1, ini + MAX_PATH - (slash + 1), "ei_rebirth.ini");
    rebirth::LoadSettings(ini);
}

void AtWinMain() {
    if (!GetModuleHandleA("addon.dll")) {
        Fail("EI_ADDON_FLAGS is set but addon.dll is not loaded (run\\vanilla-fixes: tools\\setup_fixes_run.cmd)");
        return;
    }
    LoadFlags();
    std::vector<unsigned char> file;
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!ReadFile(exe, file)) {
        Fail(std::string("cannot read ") + exe);
        return;
    }
    // .text, .rdata and .data have the same layout in the file as in memory.
    auto disk = [&](std::uintptr_t address) -> const unsigned char* {
        const std::uintptr_t offset = address - kImageBase;
        return offset < file.size() ? file.data() + offset : nullptr;
    };

    // Each patch's sites: kept when its flag is on (then every one must be live), undone otherwise.
    int liveSites[rebirth::kPatchCount] = {}, sites[rebirth::kPatchCount] = {};
    int undone = 0;
    for (const Site& site : kSites) {
        const unsigned char* original = disk(site.address + site.size - 1) ? disk(site.address) : nullptr;
        if (!original) {
            Fail("site outside game.exe's file");
            continue;
        }
        const int p = static_cast<int>(site.patch);
        ++sites[p];
        // An import slot is the loader's address on disk too: live when it points into addon.dll.
        bool live;
        if (InImportSlots(site.address)) {
            const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(site.address);
            HMODULE owner = nullptr;
            live = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(value)), &owner) &&
                   owner == GetModuleHandleA("addon.dll");
        } else {
            live = std::memcmp(ei::GamePointer<const void>(site.address), original, site.size) != 0;
        }
        if (!live) continue;
        ++liveSites[p];
        if (rebirth::Enabled(site.patch)) continue;
        if (Undo(site, original)) ++undone;
        else {
            char what[96];
            std::snprintf(what, sizeof(what), "cannot undo %s's site %08X", rebirth::Name(site.patch),
                          static_cast<unsigned>(site.address));
            Fail(what);
        }
    }
    std::string kept;
    int keptSites = 0;
    for (int p = 0; p < rebirth::kPatchCount; ++p) {
        if (!rebirth::Enabled(static_cast<Patch>(p)) || !sites[p]) continue;   // EI_Plugin's: not addon's
        const char* name = rebirth::Name(static_cast<Patch>(p));
        if (liveSites[p] != sites[p]) {
            char what[160];
            std::snprintf(what, sizeof(what), "%s is on but addon.dll applied %d of its %d sites", name, liveSites[p], sites[p]);
            Fail(what);
        }
        kept += kept.empty() ? name : std::string(" ") + name;
        keptSites += liveSites[p];
    }

    // Nothing else may differ from the file in .text and .rdata (import slots aside): only the kept sites and the
    // harness's own patches.
    int unknown = 0;
    std::string unknownList;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(Headers());
    for (WORD s = 0; s < Headers()->FileHeader.NumberOfSections; ++s) {
        if (std::strncmp(reinterpret_cast<const char*>(section[s].Name), ".text", 8) != 0 &&
            std::strncmp(reinterpret_cast<const char*>(section[s].Name), ".rdata", 8) != 0)
            continue;
        const std::uintptr_t begin = kImageBase + section[s].VirtualAddress;
        const std::uintptr_t end = begin + (std::min)(section[s].Misc.VirtualSize, section[s].SizeOfRawData);
        for (std::uintptr_t a = begin; a < end && disk(a); ++a) {
            if (*reinterpret_cast<const unsigned char*>(a) == *disk(a) || InImportSlots(a)) continue;
            std::uintptr_t b = a;   // the differing run [a, b)
            while (b < end && disk(b) && *reinterpret_cast<const unsigned char*>(b) != *disk(b)) ++b;
            bool covered = true;
            for (std::uintptr_t c = a; c < b && covered; ++c) covered = InKeptSite(c) || HarnessPatchAt(c, 1);
            if (!covered) {
                ++unknown;
                Log("addon: %08X..%08X differs from game.exe and is neither a kept site nor the harness's", static_cast<unsigned>(a),
                    static_cast<unsigned>(b));
                if (unknown <= 8) {
                    char range[32];
                    std::snprintf(range, sizeof(range), "%s%08X+%X", unknownList.empty() ? "" : " ", static_cast<unsigned>(a),
                                  static_cast<unsigned>(b - a));
                    unknownList += range;
                }
            }
            a = b;
        }
    }
    if (unknown) Fail(std::to_string(unknown) + " other change(s) in game.exe: " + unknownList);
    Log("addon: kept %s (%d sites live), undid %d site(s) of the other patches; %s", kept.empty() ? "nothing" : kept.c_str(), keptSites,
        undone, unknown ? "other changes found" : "nothing else differs from game.exe");
}

} // namespace

bool Active() {
    char value[4];
    return GetEnvironmentVariableA("EI_ADDON_FLAGS", value, sizeof(value)) == 1 && value[0] == '1';
}

void Install() {
    if (Active() && !ei::Standalone()) ei::SetOriginalWinMainAction(&AtWinMain);
}

std::string Failure() { return g_failure; }

} // namespace ei::verify::addon
