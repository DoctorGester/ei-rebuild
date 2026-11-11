// The standalone resource layer: addon.dll's mod-folder patches (Maps, ResOverride, MoviesPatch,
// CameraPatch, StreamPatch) for the standalone image. See mod_folders.h; addon's functions are named
// by their addresses in addon.dll v0.12.4.2 (image base 10000000).
#include "engine/mods/mod_folders.h"

#include "engine/core/engine_globals.h"
#include "engine/data/registry.h"
#include "engine/data/resource.h"
#include "engine/mods/maps_folder.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>


namespace modfolders {
namespace {

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
    registry.GetStringArray("Res", key, &values);
    return values;
}

ResConfig ReadResConfig() {
    ResConfig config;
    const std::string& mod = ModPath();
    if (mod.empty()) return config;
    // The game's paths (addon reads them from the starter's tree of the registry).
    const std::string resources = WithSlash(g_engineConfig.resourcesPath);
    const std::string work = WithSlash(g_engineConfig.workPath);

    FileRegistry registry;
    const std::string file = mod + "config.reg";
    if (!registry.Open(file) || registry.SectionNames().empty()) {
        return config;
    }
    // The game's archives addon knows (its table at 10009100): an archive's mod entries override it,
    // '-' entries make the game's own archive ignored. speech.res lies under Work path.
    static const char* const kOverridden[] = {"aiinfo.res", "database.Res", "databaseLmp.Res", "gfx.res",
                                              "materials.res", "outro.res", "redress.res", "sfx.res",
                                              "speech.res", "texts.res", "textsLmp.res", "textures.res"};
    for (const char* name : kOverridden) {
        const std::string key =
            Lower(text::CompareIgnoringCase("speech.res", name) == 0 ? work + "Res\\" + name : resources + name);
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

// The mod's Maps\ as addon lists it at start (1001B092): files of the top folder by name, files of
// subfolders as "<sub>\<name>"; the top folder's folders (each stands for the archives of its name, docs/notes/mods.md
// "Loose files"); whether the top folder has a quest file (mods/maps_folder.h, IsQuestFile).
struct MapsListing {
    std::vector<std::string> names;
    std::vector<std::string> folders;
    bool hasQuestList = false;
};

// What the layer reads of the mod at its first use, kept until the mod changes (Reset): the folder (ModPath), its
// Stream\ (StreamFolder), its [Res] lists (Config) and its Maps\ listing (Maps). Reset leaves the old one alive: a
// caller may hold a reference into it.
struct ModState {
    std::string path;
    std::string streamFolder;
    std::optional<ResConfig> config;
    std::optional<MapsListing> maps;
};
std::atomic<ModState*> g_state = nullptr;

ModState& State() {
    if (ModState* state = g_state.load(std::memory_order_acquire)) return *state;
    Lock lock;
    if (ModState* state = g_state.load(std::memory_order_acquire)) return *state;
    auto* state = new ModState;
    state->path = WithSlash(platform::ModFolder());
    state->streamFolder = state->path + "Stream\\";
    g_state.store(state, std::memory_order_release);
    return *state;
}

const ResConfig& Config() {
    Lock lock;
    ModState& state = State();
    if (!state.config) state.config = ReadResConfig();
    return *state.config;
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

// The mod's archives that come before `res` (none when the mod does not override it).
std::span<MappedResource* const> ArchivesBefore(const MappedResource* res) {
    ResTables& tables = Tables();
    auto overridden = tables.overridden.find(res);
    if (overridden == tables.overridden.end()) return {};
    auto mods = tables.modArchives.find(overridden->second);
    if (mods == tables.modArchives.end()) return {};
    return mods->second;
}


void ListMaps(const std::string& mod, const std::string& sub, MapsListing& list) {
    const std::string pattern = mod + "\\Maps" + sub + "\\*.*";
    for (const platform::FolderEntry& found : platform::FolderEntries(pattern)) {
        const std::string& name = found.name;
        if (found.IsDots()) continue;
        if (sub.empty() && !list.hasQuestList && IsQuestFile(found)) list.hasQuestList = true;
        if (found.folder) {
            if (sub.empty()) list.folders.push_back(name);
            ListMaps(mod, sub + "\\" + name, list);
            continue;
        }
        list.names.push_back(sub.empty() ? name : sub.substr(1) + "\\" + name);
    }
}

// Whether `listed` names `name` (_stricmp).
bool Listed(const std::vector<std::string>& listed, std::string_view name) {
    for (const std::string& each : listed)
        if (each.size() == name.size() && text::CompareIgnoringCase(each.data(), name.data(), name.size()) == 0)
            return true;
    return false;
}

const MapsListing& Maps() {
    Lock lock;
    ModState& state = State();
    if (!state.maps) {
        state.maps.emplace();
        std::string mod = state.path;
        if (!mod.empty()) {
            mod.pop_back();
            ListMaps(mod, "", *state.maps);
        }
    }
    return *state.maps;
}

bool g_mapsInMod = false;   // 100BB9D0: the game's folder until a site finds a mod file

} // namespace

// The mod the game plays ([game] Mod, platform::SwitchMod).
const std::string& ModPath() { return State().path; }

bool Active() { return !ModPath().empty(); }

void Reset() {
    Lock lock;
    ResTables& tables = Tables();
    for (auto& [key, archives] : tables.modArchives)
        for (MappedResource* archive : archives) delete archive;
    tables.modArchives.clear();
    tables.overridden.clear();
    tables.ignored.clear();
    tables.loaded = false;
    g_state.store(nullptr, std::memory_order_release);
    g_mapsInMod = false;
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
            MappedResource* res = new MappedResource();
            tables.modArchives[key].push_back(res);
            res->OpenResource(path);
        }
    }
}

void OnOpenResource(MappedResource* res, const std::string& path) {
    Lock lock;
    LoadModArchives();
    // 100203F0: the path as CharLowerA makes it (addon copies it into 0x104 bytes).
    // (kPathBytes - 1 characters at most, up to a zero byte.)
    std::string lower(path.substr(0, std::min(path.find('\0'), std::size_t{kPathBytes - 1})));
    str::MakeLower(lower);
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

// A copy: the archives are read without the lock (a switch of mods deletes them only between the game's stop and start).
std::vector<MappedResource*> ModArchivesOf(const MappedResource* res) {
    if (!Active()) return {};
    Lock lock;
    const std::span<MappedResource* const> archives = ArchivesBefore(res);
    return {archives.begin(), archives.end()};
}

bool Ignores(const MappedResource* res) {
    if (!Active()) return false;
    Lock lock;
    return Tables().ignored.contains(res);
}

MappedResource* ArchiveHolding(const MappedResource* res, void* data) {
    Lock lock;
    for (MappedResource* mod : ArchivesBefore(res))
        if (mod->filesByData.contains(data)) return mod;
    return nullptr;
}

const std::vector<std::string>& FigureArchives() { return Config().figures; }

std::vector<std::string> FigureArchivePaths(const std::string& folder) {
    std::vector<std::string> paths;
    if (Active()) paths = FigureArchives();
    paths.push_back(folder + "figures.res");
    paths.push_back(folder + "menus.res");
    return paths;
}
const std::vector<std::string>& TextureArchives() { return Config().textures; }


void SelectMaps(std::string_view name, MapsSite site) {
    if (!Active()) return;
    Lock lock;
    const MapsListing& maps = Maps();
    if (site == MapsSite::ZoneTable) {
        g_mapsInMod = maps.hasQuestList;
        return;
    }
    // 1001B2AC: _stricmp against every name; an archive's name also against the folders.
    g_mapsInMod = Listed(maps.names, name) || Listed(maps.folders, LooseFolderOf(name));
}

const std::string& MapsRoot(const std::string& dataFolder) { return g_mapsInMod ? ModPath() : dataFolder; }

// ---- Camera, Stream, Movies -----------------------------------------------------------------------

std::string CameraFile(std::string_view path) {
    // 1001B9D2: "%sCamera%s" with the mod's folder and the path from its last '\'.
    const std::size_t last = path.rfind('\\');
    if (last == std::string_view::npos || ModPath().empty()) return std::string(path);
    std::string mod = ModPath() + "Camera" + std::string(path.substr(last));
    // Ours: or its TOML (docs/notes/mods.md, "Records as TOML").
    return platform::IsFile(mod) || platform::IsFile(mod + ".toml") ? mod : std::string(path);
}

bool StreamInMod(std::string_view name) {
    // 1001BA8D: the mod's Stream\ (100BCB5C) and the name.
    if (name.empty() || ModPath().empty()) return false;
    return platform::IsFile(ModPath() + "Stream\\" + std::string(name));
}

const std::string& StreamFolder() { return State().streamFolder; }

std::string MovieFile(std::string_view name) {
    // 1001BB61: the mod's folder, "Movies" and the name from its last '\'.
    const std::size_t last = name.rfind('\\');
    if (last == std::string_view::npos || ModPath().empty()) return std::string(name);
    std::string mod = ModPath() + "Movies" + std::string(name.substr(last));
    return platform::IsFile(mod) ? mod : std::string(name);
}

} // namespace modfolders

// --- The maps folder ---------------------------------------------------------------------------------

// The maps folder and its files (MapsFolder, MapPath, MapFileExists), with Rebirth's Maps when a mod is played
// (above). See mods/maps_folder.h and docs/notes/world.md.

namespace {

constexpr const char* kMapsFolderName = "maps\\";

} // namespace

std::string MapsFolder() {
    // Rebirth's Maps (modfolders::SelectMaps names the file before each maps-folder use): the folder the last site
    // selected.
    return modfolders::MapsRoot(g_dataFolder) + kMapsFolderName;
}

std::string MapPath(std::string_view name) {
    if (name.size() > 1 && name[1] == ':') return std::string(name);
    modfolders::SelectMaps(name, modfolders::MapsSite::MapPath);
    std::string path = MapsFolder();
    path += name;
    return path;
}

bool MapFileExists(const char* name) {
    modfolders::SelectMaps(name, modfolders::MapsSite::FileExists);
    return ResourceExists(MapsFolder() + name);
}

bool IsQuestFile(const platform::FolderEntry& found) {
    if (found.folder) return platform::IsFile(found.path + "\\" + found.name + "\\quest.reg");
    const std::string_view name = found.name;
    return name.size() > 3 && text::CompareIgnoringCase(name.substr(name.size() - 3), ".mq") == 0;
}

