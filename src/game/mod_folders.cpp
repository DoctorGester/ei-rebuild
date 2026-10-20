// The standalone resource layer: addon.dll's mod-folder patches (Maps, ResOverride, MoviesPatch,
// CameraPatch, StreamPatch) for the standalone image. See mod_folders.h; addon's functions are named
// by their addresses in addon.dll v0.12.4.2 (image base 10000000).
#include "mod_folders.h"

#include "registry.h"
#include "resource.h"
#include "originals_engine.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstring>
#include <map>
#include <new>
#include <set>

#include <windows.h>

namespace modfolders {
namespace {

using ei::rebirth::Patch;

// addon guards its archive tables with one critical section (100BD5F8), held while it calls the
// archives; the layer's maps state shares it.
class Lock {
public:
    Lock() { EnterCriticalSection(Section()); }
    ~Lock() { LeaveCriticalSection(Section()); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    static CRITICAL_SECTION* Section() {
        static CRITICAL_SECTION* section = [] {
            auto* s = new CRITICAL_SECTION;
            InitializeCriticalSection(s);
            return s;
        }();
        return section;
    }
};

// A string value of the starter's settings (HKCU), or `fallback`. addon reads them with
// RegQueryValueExW; these go through the game's RegQueryValueExA, which harness runs can answer.
std::string StarterValue(const char* key, const char* name, const char* fallback) {
    std::string value = fallback;
    HKEY handle;
    if (gameapi::RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) return value;
    char text[MAX_PATH * 2];
    DWORD size = sizeof(text) - 1, type = 0;
    if (gameapi::RegQueryValueExA(handle, name, nullptr, &type, reinterpret_cast<BYTE*>(text), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        text[size] = '\0';
        value = text;
    }
    gameapi::RegCloseKey(handle);
    return value;
}

// addon's 10025C9D: a '\' added to a path that does not end in one (an empty one stays empty).
std::string WithSlash(std::string path) {
    if (!path.empty() && path.back() != '\\') path += '\\';
    return path;
}

// addon's 10025B50: the archive table's key, CharLowerA's lower case of the path.
std::string Lower(std::string text) {
    if (!text.empty()) CharLowerA(text.data());
    return text;
}

// addon's 1001B9B3: the path names a file (GetFileAttributes; not a folder).
bool FileExists(const std::string& path) {
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// ---- ResOverride ---------------------------------------------------------------------------------

// The mod's [Res] lists (addon's 100090B7, read when addon loads): keys are the lower-case paths
// the game opens its archives by, values the mod's archives as full paths.
struct ResConfig {
    std::map<std::string, std::vector<std::string>> overrides;   // 100BD27C
    std::set<std::string> ignored;                               // 100BD2AC ('-' entries)
    std::vector<std::string> figures;                            // 100BD28C
    std::vector<std::string> textures;                           // 100BD29C
};

// The strings of a [Res] value (a string or an array of them).
std::vector<std::string> ResValues(CFileRegistry& registry, const char* key) {
    std::vector<std::string> values;
    int isScalar = 0;
    const std::uint8_t* value = registry.FindData("Res", key, kRegArray | kRegString, &isScalar);
    if (!value) return values;
    int count = 1;
    if (!isScalar) {
        count = value[0] | (value[1] << 8);
        value += 2;
    }
    for (int i = 0; i < count; ++i) {
        int length = value[0] | (value[1] << 8);
        values.emplace_back(reinterpret_cast<const char*>(value + 2), static_cast<std::size_t>(length));
        value += length + 2;
    }
    return values;
}

ResConfig ReadResConfig() {
    ResConfig config;
    const std::string& mod = ModPath();
    if (mod.empty()) return config;
    constexpr const char* kPathSettings = "Software\\Gipat.Ru\\EI_Starter\\EvilIslands\\Path Settings";
    const std::string resources = WithSlash(StarterValue(kPathSettings, "RESOURCES PATH", ".\\Res\\"));
    const std::string cd = WithSlash(StarterValue(kPathSettings, "CD-ROM PATH", ".\\"));

    CFileRegistry registry;
    registry.Construct();
    const std::string file = mod + "config.reg";
    if (!registry.Open(file.c_str()) || (registry.data_[4] | (registry.data_[5] << 8)) == 0) {
        registry.Destroy();
        return config;
    }
    // The game's archives addon knows (its table at 10009100): an archive's mod entries override it,
    // '-' entries make the game's own archive ignored. speech.res lies on the CD.
    static const char* const kOverridden[] = {"aiinfo.res", "database.Res", "databaseLmp.Res", "gfx.res",
                                              "materials.res", "outro.res", "redress.res", "sfx.res",
                                              "speech.res", "texts.res", "textsLmp.res", "textures.res"};
    for (const char* name : kOverridden) {
        const std::string key = Lower(_stricmp("speech.res", name) == 0 ? cd + "Res\\" + name : resources + name);
        std::vector<std::string> paths;
        for (const std::string& value : ResValues(registry, name)) {
            if (!value.empty() && value[0] == '-')
                config.ignored.insert(key);
            else
                paths.push_back(mod + value);
        }
        if (!paths.empty()) config.overrides[key] = paths;
    }
    // The figure manager's and the texture manager's extra archives.
    auto extra = [&](std::initializer_list<const char*> names, std::vector<std::string>& out) {
        for (const char* name : names) {
            const std::string key = Lower(resources + name);
            for (const std::string& value : ResValues(registry, name)) {
                if (!value.empty() && value[0] == '-')
                    config.ignored.insert(key);
                else
                    out.push_back(mod + value);
            }
        }
    };
    extra({"figures.res", "menus.res"}, config.figures);
    extra({"textures.res", "redress.res", "menus.res"}, config.textures);
    registry.Destroy();
    return config;
}

const ResConfig& Config() {
    static const ResConfig config = ReadResConfig();
    return config;
}

// The open archives addon tracks (100BD598 / 100BD5B8 / 100BD5A8 / 100BD5D8).
struct ResTables {
    bool loaded = false;                                                   // 100BD129
    std::map<std::string, std::vector<CMappedResource*>> modArchives;     // key -> the mod's archives
    std::map<const CMappedResource*, std::string> overridden;             // archive -> its key
    std::set<const CMappedResource*> ignored;
};
ResTables& Tables() {
    static ResTables* tables = new ResTables;
    return *tables;
}

// One lookup of 1002005E: the mod's archives for `res`, then `res` itself unless ignored.
void* FindFile(CMappedResource* res, const char* name, std::uint32_t* size) {
    ResTables& tables = Tables();
    auto overridden = tables.overridden.find(res);
    if (overridden != tables.overridden.end()) {
        auto mods = tables.modArchives.find(overridden->second);
        if (mods != tables.modArchives.end()) {
            for (CMappedResource* mod : mods->second)
                if (void* data = mod->GetFilePlain(name, size)) return data;
        }
    }
    if (tables.ignored.count(res) != 0) return nullptr;
    return res->GetFilePlain(name, size);
}

// ---- Maps -----------------------------------------------------------------------------------------

// The mod's Maps\ as addon lists it at start (1001B092): files of the top folder by name, files of
// subfolders as "<sub>\<name>"; whether the top folder has a ".mq" file (case-sensitive).
struct MapsListing {
    std::vector<std::string> names;
    bool hasQuestList = false;
};

void ListMaps(const std::string& mod, const std::string& sub, MapsListing& list) {
    const std::string pattern = mod + "\\Maps" + sub + "\\*.*";
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(pattern.c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (std::strcmp(found.cFileName, ".") != 0 && std::strcmp(found.cFileName, "..") != 0)
                ListMaps(mod, sub + "\\" + found.cFileName, list);
            continue;
        }
        if (!sub.empty()) {
            list.names.push_back(sub.substr(1) + "\\" + found.cFileName);
            continue;
        }
        const std::size_t length = std::strlen(found.cFileName);
        if (!list.hasQuestList && length >= 3 && std::strcmp(found.cFileName + length - 3, ".mq") == 0)
            list.hasQuestList = true;
        list.names.push_back(found.cFileName);
    } while (FindNextFileA(find, &found));
    FindClose(find);
}

const MapsListing& Maps() {
    static const MapsListing list = [] {
        MapsListing l;
        std::string mod = ModPath();
        if (mod.empty()) return l;
        mod.pop_back();
        ListMaps(mod, "", l);
        return l;
    }();
    return list;
}

bool g_mapsInMod = false;   // 100BB9D0: the game's folder (007AF460) until a site finds a mod file

} // namespace

bool Active(Patch p) { return ei::Standalone() && ei::rebirth::Enabled(p); }

const std::string& ModPath() {
    static const std::string path =
        WithSlash(StarterValue("Software\\Gipat.Ru\\EI_Starter\\Settings", "AddonPath", ""));
    return path;
}

// ---- ResOverride ----------------------------------------------------------------------------------

void LoadModArchives() {
    Lock lock;
    ResTables& tables = Tables();
    if (tables.loaded) return;
    tables.loaded = true;
    // 1001FD2E: each mod archive is a new CMappedResource (00671280) opened read-only through
    // OpenResource's address (00671730, whose entry records it like any other).
    for (const auto& [key, paths] : Config().overrides) {
        for (const std::string& path : paths) {
            void* memory = ::operator new(sizeof(CMappedResource));
            std::memset(memory, 0, sizeof(CMappedResource));
            CMappedResource* res = reinterpret_cast<CMappedResource*>(memory)->Construct();
            tables.modArchives[key].push_back(res);
            MappedResourceOpenOriginal(res, nullptr, path.c_str(), 0);
        }
    }
}

void OnOpenResource(CMappedResource* res, const char* path) {
    Lock lock;
    LoadModArchives();
    // 100203F0: the path as CharLowerA makes it (addon copies it into 0x104 bytes).
    char lower[MAX_PATH];
    strncpy_s(lower, path, _TRUNCATE);
    CharLowerA(lower);
    const ResConfig& config = Config();
    ResTables& tables = Tables();
    if (config.ignored.count(lower) != 0) tables.ignored.insert(res);
    if (config.overrides.count(lower) != 0) tables.overridden[res] = lower;
}

void OnDestroyResource(const CMappedResource* res) {
    Lock lock;
    ResTables& tables = Tables();
    tables.overridden.erase(res);
    tables.ignored.erase(res);
}

void* GetFile(CMappedResource* res, const char* name, std::uint32_t* size) {
    Lock lock;
    void* data = FindFile(res, name, size);
    // 1002012F: a "t\" or "s\" name not found is looked up once more without the prefix.
    if (!data && name && (name[0] == 't' || name[0] == 's') && name[1] == '\\') data = FindFile(res, name + 2, size);
    return data;
}

void ListFiles(const CMappedResource* res, mfc::CStringArray* out, const char* pattern) {
    Lock lock;
    // 100201FD: each listing goes to a scratch array (ListFiles refills it) and is appended to `out`.
    mfc::CStringArray names;
    names.Construct();
    names.SetSize(0, -1);
    ResTables& tables = Tables();
    auto overridden = tables.overridden.find(res);
    if (overridden != tables.overridden.end()) {
        auto mods = tables.modArchives.find(overridden->second);
        if (mods != tables.modArchives.end()) {
            for (const CMappedResource* mod : mods->second) {
                mod->ListFilesPlain(&names, pattern);
                out->Append(names);
            }
        }
    }
    if (tables.ignored.count(res) == 0) {
        res->ListFilesPlain(&names, pattern);
        out->Append(names);
    }
    names.Destruct();
}

const std::vector<std::string>& FigureArchives() { return Config().figures; }
const std::vector<std::string>& TextureArchives() { return Config().textures; }

// ---- Maps -----------------------------------------------------------------------------------------

void SelectMaps(const char* name, MapsSite site) {
    if (!Active(Patch::Maps)) return;
    Lock lock;
    const MapsListing& maps = Maps();
    if (site == MapsSite::ZoneTable) {
        g_mapsInMod = maps.hasQuestList;
        return;
    }
    // 1001B2AC: _stricmp against every name.
    g_mapsInMod = false;
    if (!name) return;
    for (const std::string& listed : maps.names) {
        if (_stricmp(listed.c_str(), name) == 0) {
            g_mapsInMod = true;
            return;
        }
    }
}

const mfc::CString& MapsRoot(const mfc::CString& dataFolder) {
    static const mfc::CString* modRoot = new mfc::CString(ModPath().c_str());
    return g_mapsInMod ? *modRoot : dataFolder;
}

// ---- Camera, Stream, Movies -----------------------------------------------------------------------

std::string CameraFile(const char* path) {
    // 1001B9D2: "%sCamera%s" with the mod's folder and the path from its last '\'.
    const char* last = std::strrchr(path, '\\');
    if (!last || ModPath().empty()) return path;
    std::string mod = ModPath() + "Camera" + last;
    return FileExists(mod) ? mod : std::string(path);
}

bool StreamInMod(const char* name) {
    // 1001BA8D: the mod's Stream\ (100BCB5C) and the name.
    if (!name || !*name || ModPath().empty()) return false;
    return FileExists(ModPath() + "Stream\\" + name);
}

const mfc::CString& StreamFolder() {
    static const mfc::CString* folder = new mfc::CString((ModPath() + "Stream\\").c_str());
    return *folder;
}

std::string MovieFile(const char* name) {
    // 1001BB61: the mod's folder, "Movies" and the name from its last '\'.
    const char* last = std::strrchr(name, '\\');
    if (!last || ModPath().empty()) return name;
    std::string mod = ModPath() + "Movies" + last;
    return FileExists(mod) ? mod : std::string(name);
}

} // namespace modfolders
