// The main menu's list of mods (mod_list.h).
#include "engine/mods/mod_list.h"

#include "engine/data/registry.h"

#include "lib/text_case.h"
#include "platform/path_layers.h"
#include "platform/platform.h"

#include <algorithm>
#include <string_view>

namespace modlist {
namespace {

constexpr const char* kModsFolder = "starter\\Mods";
constexpr const char* kNoMod = "No mod";

std::string WithoutSlash(std::string folder) {
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

bool IsZip(std::string_view name) {
    return name.size() > 4 && text::CompareIgnoringCase(name.substr(name.size() - 4), ".zip") == 0;
}

bool SameFolder(const std::string& a, const std::string& b) {
    return !a.empty() && text::CompareIgnoringCase(WithoutSlash(a), WithoutSlash(b)) == 0;
}

// The entry for the mod at `path` (a folder or a .zip: platform::ModRoot), titled `name` when its config.reg has no
// title; false when it has no config.reg.
bool ModEntry(const std::string& path, const std::string& name, Entry* entry) {
    const std::string root = platform::ModRoot(path);
    FileRegistry registry;
    if (root.empty() || !registry.Open(root + "\\config.reg")) return false;
    entry->folder = root;
    entry->setting = SettingOf(platform::FullGamePath(path));
    entry->title = name;
    const std::string title = registry.GetString("MOD", "Title", "");
    if (!title.empty()) entry->title = title;
    const int single = registry.GetInt("MOD", "Single", -1);
    const int multi = registry.GetInt("MOD", "Multi", -1);
    if (single == 1 && multi == 0) entry->title += " (single player)";
    else if (single == 0 && multi == 1) entry->title += " (multiplayer)";
    return true;
}

} // namespace

std::vector<Entry> Scan(const std::string& gameFolder, const std::string& current) {
    std::vector<Entry> entries;
    entries.push_back(Entry{kNoMod, {}, {}});
    std::vector<platform::FolderEntry> mods;
    for (platform::FolderEntry& found : platform::FolderEntries(WithoutSlash(gameFolder) + "\\" + kModsFolder + "\\*"))
        if (found.folder ? !found.IsDots() : IsZip(found.name)) mods.push_back(std::move(found));
    std::sort(mods.begin(), mods.end(), [](const platform::FolderEntry& a, const platform::FolderEntry& b) {
        return text::CompareIgnoringCase(a.name, b.name) < 0;
    });
    for (const platform::FolderEntry& mod : mods) {
        Entry entry;
        if (ModEntry(mod.path, mod.folder ? mod.name : mod.Title(), &entry)) entries.push_back(std::move(entry));
    }
    if (!current.empty() && IndexOf(entries, current) == 0) {
        const std::string folder = WithoutSlash(current);
        const std::string name = folder.substr(folder.find_last_of('\\') + 1);
        Entry entry;
        if (!ModEntry(folder, name, &entry)) {
            entry.folder = platform::FullGamePath(folder);
            entry.setting = SettingOf(entry.folder);
            entry.title = name;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

int IndexOf(const std::vector<Entry>& entries, const std::string& current) {
    for (std::size_t i = 1; i < entries.size(); ++i)
        if (SameFolder(entries[i].folder, current)) return static_cast<int>(i);
    return 0;
}

std::string SettingOf(const std::string& folder) {
    const std::string program = platform::ProgramPath();
    const std::string exeFolder = platform::FullGamePath(program.substr(0, program.find_last_of("\\/")));
    std::string_view rest;
    if (!exeFolder.empty() && platform::PathBelow(folder, exeFolder, &rest) && !rest.empty()) return std::string(rest);
    return folder;
}

} // namespace modlist
