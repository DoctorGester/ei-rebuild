// The standalone resource layer: addon.dll's mod-folder patches (Maps, ResOverride, MoviesPatch,
// CameraPatch, StreamPatch) for the standalone image. See mod_folders.h; addon's functions are named
// by their addresses in addon.dll v0.12.4.2 (image base 10000000).
#include "mod_folders.h"

#include "registry.h"
#include "resource.h"
#include "settings_file.h"
#include "lib/text_case.h"

#include "platform/files.h"

#include "ei/hook.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

#include <map>
#include <mutex>
#include <set>


namespace modfolders {
namespace {

using ei::rebirth::Patch;

constexpr std::size_t kPathBytes = 260;   // addon's path buffers (MAX_PATH)

// addon guards its archive tables with one critical section (100BD5F8), held while it calls the
// archives; the layer's maps state shares it.
class Lock {
public:
    Lock() { Section()->lock(); }
    ~Lock() { Section()->unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    static std::recursive_mutex* Section() {
        static std::recursive_mutex* section = new std::recursive_mutex;
        return section;
    }
};

// A string value of the starter's settings (HKCU; the standalone's settings file, settings_file.h), or
// `fallback`. addon reads them with RegQueryValueExW.
std::string StarterValue(const char* key, const char* name, const char* fallback) {
    std::string value = fallback;
    char text[kPathBytes * 2];
    std::uint32_t size = sizeof(text) - 1, type = 0;
    if (settings_file::ReadValue(key, name, &type, text, &size) == settings_file::kSuccess && (type == settings_file::kTypeString || type == settings_file::kTypeExpandString)) {
        text[size] = '\0';
        value = text;
    }
    return value;
}

// addon's 10025C9D: a '\' added to a path that does not end in one (an empty one stays empty).
std::string WithSlash(std::string path) {
    if (!path.empty() && path.back() != '\\') path += '\\';
    return path;
}

// addon's 10025B50: the archive table's key, CharLowerA's lower case of the path.
std::string Lower(std::string text) {
    if (!text.empty()) text::LowerInPlace(text.data());
    return text;
}


// The mod's [Res] lists (addon's 100090B7, read when addon loads): keys are the lower-case paths
// the game opens its archives by, values the mod's archives as full paths.
struct ResConfig {
    std::map<std::string, std::vector<std::string>> overrides;   // 100BD27C
    std::set<std::string> ignored;                               // 100BD2AC ('-' entries)
    std::vector<std::string> figures;                            // 100BD28C
    std::vector<std::string> textures;                           // 100BD29C
};

// The strings of a [Res] value (a string or an array of them).
std::vector<std::string> ResValues(FileRegistry& registry, const char* key) {
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

    FileRegistry registry;
    const std::string file = mod + "config.reg";
    if (!registry.Open(file) || (registry.data[4] | (registry.data[5] << 8)) == 0) {
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
    return config;
}

const ResConfig& Config() {
    static const ResConfig config = ReadResConfig();
    return config;
}

// The open archives addon tracks (100BD598 / 100BD5B8 / 100BD5A8 / 100BD5D8).
struct ResTables {
    bool loaded = false;                                                   // 100BD129
    std::map<std::string, std::vector<MappedResource*>> modArchives;     // key -> the mod's archives
    std::map<const MappedResource*, std::string> overridden;             // archive -> its key
    std::set<const MappedResource*> ignored;
};
ResTables& Tables() {
    static ResTables* tables = new ResTables;
    return *tables;
}

// One lookup of 1002005E: the mod's archives for `res`, then `res` itself unless ignored.
void* FindFile(MappedResource* res, std::string_view name, std::uint32_t* size) {
    ResTables& tables = Tables();
    auto overridden = tables.overridden.find(res);
    if (overridden != tables.overridden.end()) {
        auto mods = tables.modArchives.find(overridden->second);
        if (mods != tables.modArchives.end()) {
            for (MappedResource* mod : mods->second)
                if (void* data = mod->GetFilePlain(name, size)) return data;
        }
    }
    if (tables.ignored.count(res) != 0) return nullptr;
    return res->GetFilePlain(name, size);
}


// The mod's Maps\ as addon lists it at start (1001B092): files of the top folder by name, files of
// subfolders as "<sub>\<name>"; whether the top folder has a ".mq" file (case-sensitive).
struct MapsListing {
    std::vector<std::string> names;
    bool hasQuestList = false;
};

void ListMaps(const std::string& mod, const std::string& sub, MapsListing& list) {
    const std::string pattern = mod + "\\Maps" + sub + "\\*.*";
    for (const platform::FolderEntry& found : platform::FolderEntries(pattern)) {
        const char* name = found.name.c_str();
        if (found.folder) {
            if (std::strcmp(name, ".") != 0 && std::strcmp(name, "..") != 0) ListMaps(mod, sub + "\\" + name, list);
            continue;
        }
        if (!sub.empty()) {
            list.names.push_back(sub.substr(1) + "\\" + name);
            continue;
        }
        const std::size_t length = found.name.size();
        if (!list.hasQuestList && length >= 3 && std::strcmp(name + length - 3, ".mq") == 0) list.hasQuestList = true;
        list.names.push_back(found.name);
    }
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

bool g_mapsInMod = false;   // 100BB9D0: the game's folder until a site finds a mod file

} // namespace

const std::string& ModPath() {
    static const std::string path =
        WithSlash(StarterValue("Software\\Gipat.Ru\\EI_Starter\\Settings", "AddonPath", ""));
    return path;
}


void LoadModArchives() {
    Lock lock;
    ResTables& tables = Tables();
    if (tables.loaded) return;
    tables.loaded = true;
    // 1001FD2E: each mod archive is a new MappedResource opened read-only through
    // OpenResource's address (whose entry records it like any other).
    for (const auto& [key, paths] : Config().overrides) {
        for (const std::string& path : paths) {
            void* memory = ::operator new(sizeof(MappedResource));
            std::memset(memory, 0, sizeof(MappedResource));
            MappedResource* res = new (memory) MappedResource();
            tables.modArchives[key].push_back(res);
            res->MappedResource::OpenResource(path, 0);
        }
    }
}

void OnOpenResource(MappedResource* res, const std::string& path) {
    Lock lock;
    LoadModArchives();
    // 100203F0: the path as CharLowerA makes it (addon copies it into 0x104 bytes).
    char lower[kPathBytes];
    strncpy_s(lower, path.c_str(), _TRUNCATE);
    text::LowerInPlace(lower);
    const ResConfig& config = Config();
    ResTables& tables = Tables();
    if (config.ignored.count(lower) != 0) tables.ignored.insert(res);
    if (config.overrides.count(lower) != 0) tables.overridden[res] = lower;
}

void OnDestroyResource(const MappedResource* res) {
    Lock lock;
    ResTables& tables = Tables();
    tables.overridden.erase(res);
    tables.ignored.erase(res);
}

void* GetFile(MappedResource* res, std::string_view name, std::uint32_t* size) {
    Lock lock;
    void* data = FindFile(res, name, size);
    // 1002012F: a "t\" or "s\" name not found is looked up once more without the prefix.
    if (!data && name.size() >= 2 && (name[0] == 't' || name[0] == 's') && name[1] == '\\') data = FindFile(res, name.substr(2), size);
    return data;
}

void ListFiles(const MappedResource* res, std::vector<std::string>* out, const char* pattern) {
    Lock lock;
    // 100201FD: each archive's listing is appended to `out`.
    ResTables& tables = Tables();
    auto overridden = tables.overridden.find(res);
    if (overridden != tables.overridden.end()) {
        auto mods = tables.modArchives.find(overridden->second);
        if (mods != tables.modArchives.end()) {
            for (const MappedResource* mod : mods->second) {
                const std::vector<std::string> names = mod->ListFilesPlain(pattern);
                out->insert(out->end(), names.begin(), names.end());
            }
        }
    }
    if (tables.ignored.count(res) == 0) {
        const std::vector<std::string> names = res->ListFilesPlain(pattern);
        out->insert(out->end(), names.begin(), names.end());
    }
}

const std::vector<std::string>& FigureArchives() { return Config().figures; }
const std::vector<std::string>& TextureArchives() { return Config().textures; }


void SelectMaps(std::string_view name, MapsSite site) {
    if (!ei::rebirth::Enabled(Patch::Maps)) return;
    Lock lock;
    const MapsListing& maps = Maps();
    if (site == MapsSite::ZoneTable) {
        g_mapsInMod = maps.hasQuestList;
        return;
    }
    // 1001B2AC: _stricmp against every name.
    g_mapsInMod = false;
    for (const std::string& listed : maps.names) {
        if (listed.size() == name.size() && _strnicmp(listed.data(), name.data(), name.size()) == 0) {
            g_mapsInMod = true;
            return;
        }
    }
}

const std::string& MapsRoot(const std::string& dataFolder) {
    static const std::string* modRoot = new std::string(ModPath());
    return g_mapsInMod ? *modRoot : dataFolder;
}

// ---- Camera, Stream, Movies -----------------------------------------------------------------------

std::string CameraFile(const char* path) {
    // 1001B9D2: "%sCamera%s" with the mod's folder and the path from its last '\'.
    const char* last = std::strrchr(path, '\\');
    if (!last || ModPath().empty()) return path;
    std::string mod = ModPath() + "Camera" + last;
    return platform::IsFile(mod) ? mod : std::string(path);
}

bool StreamInMod(std::string_view name) {
    // 1001BA8D: the mod's Stream\ (100BCB5C) and the name.
    if (name.empty() || ModPath().empty()) return false;
    return platform::IsFile(ModPath() + "Stream\\" + std::string(name));
}

const std::string& StreamFolder() {
    static const std::string* folder = new std::string((ModPath() + "Stream\\"));
    return *folder;
}

std::string MovieFile(const char* name) {
    // 1001BB61: the mod's folder, "Movies" and the name from its last '\'.
    const char* last = std::strrchr(name, '\\');
    if (!last || ModPath().empty()) return name;
    std::string mod = ModPath() + "Movies" + last;
    return platform::IsFile(mod) ? mod : std::string(name);
}

} // namespace modfolders
