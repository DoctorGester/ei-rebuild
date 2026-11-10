// The layered path map (path_layers.h; docs/notes/mods.md) and the mod the game plays (platform.h, SwitchMod).
#include "platform/path_layers.h"

#include "lib/text_case.h"
#include "platform/zip_folders.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <system_error>

namespace platform {

namespace {

namespace fs = std::filesystem;

// A zip's files and folders too (zip_folders.h).
bool Exists(const std::string& path) {
    ZipPlace place;
    if (InZip(path, &place)) return place.found;
    std::error_code error;
    return fs::exists(NativePath(path), error);
}

bool IsFolderPath(const std::string& path) {
    ZipPlace place;
    if (InZip(path, &place)) return place.folder;
    std::error_code error;
    return fs::is_directory(NativePath(path), error);
}

std::string Joined(std::string_view folder, std::string_view relative) {
    std::string path(folder);
    if (relative.empty()) return path;
    if (path.empty()) return std::string(relative);
    if (path.back() != '\\') path += '\\';
    path += relative;
    return path;
}

// The folders of `file` made (below the first that exists).
void MakeFoldersOf(const std::string& file) {
    const std::size_t cut = file.find_last_of('\\');
    if (cut == std::string::npos || cut == 0) return;
    std::error_code error;
    fs::create_directories(NativePath(file.substr(0, cut)), error);
}

// Whether `relative` is `folder` or below it ("saves", "saves\save1").
bool InFolder(std::string_view relative, std::string_view folder) {
    return relative.size() >= folder.size() &&
           text::CompareIgnoringCase(relative.data(), folder.data(), folder.size(), false) == 0 &&
           (relative.size() == folder.size() || relative[folder.size()] == '\\');
}

constexpr std::string_view kSaves = "saves";

std::atomic<const Mod*> g_mod{nullptr};

} // namespace

std::string FullGamePath(std::string_view path) {
    std::string text(path);
    std::replace(text.begin(), text.end(), '/', '\\');
    if (text.empty()) return {};
    std::string root;   // the part ".." never removes: "C:\", "\\" (a share's start) or "\"
    std::size_t at = 0;
    if (text.size() >= 2 && text[1] == ':') {
        if (text.size() < 3 || text[2] != '\\') return {};
        root = text.substr(0, 3);
        at = 3;
    } else if (text.starts_with("\\\\")) {
        root = "\\\\";
        at = 2;
    } else if (text[0] == '\\') {
        // The current drive's root on Windows, the system's root elsewhere.
        const std::string current = CurrentFolder();
        root = current.size() >= 2 && current[1] == ':' ? current.substr(0, 2) + "\\" : "\\";
        at = 1;
    } else {
        const std::string current = CurrentFolder();
        if (current.empty()) return {};
        return FullGamePath(Joined(current, text));
    }
    std::vector<std::string_view> parts;
    const std::string_view rest = std::string_view(text).substr(at);
    for (std::size_t start = 0; start <= rest.size();) {
        const std::size_t end = std::min(rest.find('\\', start), rest.size());
        const std::string_view part = rest.substr(start, end - start);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        start = end + 1;
    }
    std::string full = root;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) full += '\\';
        full += parts[i];
    }
    return full;
}

bool PathBelow(std::string_view full, std::string_view folder, std::string_view* rest) {
    std::size_t n = folder.size();
    if (n == 0 || full.size() < n || text::CompareIgnoringCase(full.data(), folder.data(), n, false) != 0) return false;
    if (folder.back() != '\\') {
        if (full.size() > n && full[n] != '\\') return false;
        if (full.size() > n) ++n;
    }
    *rest = full.substr(n);
    return true;
}

std::string PathLayer::Place(std::string_view relative) const { return place ? place(relative) : Joined(folder, relative); }

bool PathLayers::Relative(const std::string& full, std::string* relative) const {
    std::string_view rest;
    if (!saves.empty() && PathBelow(full, saves, &rest)) return false;
    if (writesToTop && !layers.empty() && PathBelow(full, layers.front().folder, &rest)) return false;
    for (const std::string& base : bases) {
        if (PathBelow(full, base, &rest)) {
            *relative = rest;
            return true;
        }
    }
    return false;
}

std::string PathLayers::Find(std::string_view relative, std::size_t from, std::size_t* found) const {
    for (std::size_t i = from; i < layers.size(); ++i) {
        const PathLayer& layer = layers[i];
        std::string path = layer.Place(relative);
        if (path.empty()) continue;
        if (Exists(path) || (layer.owns && layer.owns(relative))) {
            if (found) *found = i;
            return path;
        }
    }
    if (found) *found = layers.size();
    return {};
}

std::string PathLayers::Map(std::string_view path, PathUse use, Placed* placed) const {
    if (placed) *placed = Placed::Kept;
    const std::string full = FullGamePath(path);
    std::string relative;
    if (full.empty() || !Relative(full, &relative)) return std::string(path);
    if (!saves.empty() && InFolder(relative, kSaves)) {
        if (placed) *placed = Placed::Saves;
        if (use != PathUse::Read && !Exists(saves)) {
            std::error_code error;
            fs::create_directories(NativePath(saves), error);
        }
        return Joined(saves, std::string_view(relative).substr(std::min(relative.size(), kSaves.size() + 1)));
    }
    // A read, or the game's write: the top layer that has the file.
    if (use == PathUse::Read || !writesToTop || layers.empty()) {
        std::size_t at = 0;
        std::string found = relative.empty() ? std::string() : Find(relative, 0, &at);
        if (found.empty()) return std::string(path);
        if (placed) *placed = Placed::Layer;
        if (use != PathUse::Read && layers[at].readOnly && at > 0) {
            // A zip's file is written above it, starting from its bytes for an Update.
            const std::string target = layers[at - 1].Place(relative);
            if (target.empty()) return found;
            MakeFoldersOf(target);
            if (use == PathUse::Update && !Exists(target)) CopySystemFile(found, target, false);
            return target;
        }
        return found;
    }
    // The game folder's own write (a search's "..": the empty folder removed) is the top layer's folder.
    std::string target = layers.front().Place(relative);
    if (target.empty()) return std::string(path);
    if (placed) *placed = Placed::Layer;
    if (relative.empty()) return target;
    MakeFoldersOf(target);
    if (use == PathUse::Update && !Exists(target)) {
        std::string from = Find(relative, 1);
        if (from.empty()) from = path;
        if (Exists(from) && (!copiesUp || copiesUp(relative, from))) CopySystemFile(from, target, false);
    }
    return target;
}

std::vector<FolderSearch> PathLayers::Searches(std::string_view pattern) const {
    const std::size_t slash = pattern.find_last_of("\\/");
    const std::string_view folder = slash == std::string_view::npos ? std::string_view(".") : pattern.substr(0, slash);
    const std::string_view mask = slash == std::string_view::npos ? pattern : pattern.substr(slash + 1);
    const std::string full = FullGamePath(folder);
    std::string relative;
    if (full.empty() || !Relative(full, &relative) || (!saves.empty() && InFolder(relative, kSaves)))
        return {{Map(pattern, PathUse::Read), {}}};
    std::vector<FolderSearch> searches;
    const std::string relativePattern = Joined(relative, mask);
    for (const PathLayer& layer : layers) {
        const std::string place = layer.Place(relative);
        if (place.empty()) continue;
        if (layer.owns && layer.owns(relativePattern)) {
            searches.push_back({Joined(place, mask), {}});
            return searches;
        }
        if (!IsFolderPath(place)) continue;
        searches.push_back({Joined(place, mask), [layer, relative](std::string_view name) {
                                return !layer.Place(Joined(relative, name)).empty();
                            }});
    }
    searches.push_back({std::string(pattern), {}});
    return searches;
}

// ---- The mod -------------------------------------------------------------------------------------------------------

const Mod* CurrentMod() { return g_mod.load(std::memory_order_acquire); }

bool SwitchMod(std::string_view gameFolder, std::string_view modFolder) {
    // The mod played so far is never deleted: other threads may still map a path through it.
    const std::string game = FullGamePath(gameFolder);
    const std::string folder = ModRoot(modFolder);
    if (game.empty() || folder.empty()) {
        g_mod.store(nullptr, std::memory_order_release);
        return modFolder.empty();
    }
    auto* mod = new Mod;
    mod->folder = folder;
    mod->name = folder.substr(folder.find_last_of('\\') + 1);
    if (InZip(folder) && mod->name.size() > 4 &&
        text::CompareIgnoringCase(std::string_view(mod->name).substr(mod->name.size() - 4), ".zip") == 0)
        mod->name.resize(mod->name.size() - 4);   // the zip itself is the mod's folder
    // The mod's folders' files, not its root's (config.reg, its readme, Rebirth's plugin) and no archive.
    mod->layer.folder = folder;
    mod->layer.place = [folder](std::string_view relative) -> std::string {
        if (relative.empty()) return folder;
        const std::string path = Joined(folder, relative);
        if (relative.size() >= 4 && text::CompareIgnoringCase(relative.substr(relative.size() - 4), ".res") == 0) return {};
        if (relative.find('\\') == std::string_view::npos && !IsFolderPath(path)) return {};
        return path;
    };
    mod->map.bases = {game};
    mod->map.saves = Joined(Joined(game, kSaves), mod->name);
    mod->map.layers = {mod->layer};
    if (InZip(folder)) {
        // A zip is read only: what the game writes for it (config	utorial.ini, addrbook.ini) is kept in the mod's
        // saves folder, standing over the zip.
        mod->layer.readOnly = true;
        PathLayer written;
        written.folder = Joined(mod->map.saves, "files");
        mod->map.layers = {written, mod->layer};
    }
    g_mod.store(mod, std::memory_order_release);
    return true;
}

std::string ModFolder() {
    const Mod* mod = CurrentMod();
    return mod ? mod->folder : std::string();
}

std::string ModName() {
    const Mod* mod = CurrentMod();
    return mod ? mod->name : std::string();
}

std::string ModRoot(std::string_view path) {
    const std::string named = FullGamePath(path);
    const std::string full = named.empty() ? named : FullGamePath(SystemPath(named, PathUse::Read));
    if (full.empty() || !IsFolderPath(full)) return {};
    if (Exists(Joined(full, "config.reg"))) return full;
    const std::string pattern = Joined(full, "*");
    std::vector<FolderEntry> entries;
    for (FolderEntry& entry : SearchFolder(pattern, pattern))
        if (!entry.IsDots()) entries.push_back(std::move(entry));
    if (entries.size() == 1 && entries[0].folder && Exists(Joined(entries[0].path, "config.reg"))) return entries[0].path;
    return full;
}

} // namespace platform
