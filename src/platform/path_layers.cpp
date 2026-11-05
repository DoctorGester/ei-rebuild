// The layered path map (path_layers.h; docs/notes/mods.md) and the mod the game plays (platform.h, UseMod).
#include "platform/path_layers.h"

#include "lib/text_case.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <system_error>

namespace platform {

namespace {

namespace fs = std::filesystem;

bool Exists(const std::string& path) {
    std::error_code error;
    return fs::exists(NativePath(path), error);
}

bool IsFolderPath(const std::string& path) {
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

std::string PathLayers::Find(std::string_view relative, std::size_t from) const {
    for (std::size_t i = from; i < layers.size(); ++i) {
        const PathLayer& layer = layers[i];
        std::string path = layer.Place(relative);
        if (path.empty()) continue;
        if (Exists(path) || (layer.owns && layer.owns(relative))) return path;
    }
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
    if (use == PathUse::Read) {
        std::string found = relative.empty() ? std::string() : Find(relative, 0);
        if (found.empty()) return std::string(path);
        if (placed) *placed = Placed::Layer;
        return found;
    }
    if (!writesToTop || layers.empty()) {
        std::string found = relative.empty() ? std::string() : Find(relative, 0);
        if (found.empty()) return std::string(path);
        if (placed) *placed = Placed::Layer;
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
        if (Exists(from) && (!copiesUp || copiesUp(relative, from))) {
            std::error_code error;
            fs::copy_file(NativePath(from), NativePath(target), fs::copy_options::none, error);
        }
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

bool UseMod(std::string_view gameFolder, std::string_view modFolder) {
    const std::string game = FullGamePath(gameFolder);
    const std::string folder = FullGamePath(modFolder);
    if (game.empty() || folder.empty() || !IsFolderPath(folder) || CurrentMod()) return false;
    auto* mod = new Mod;   // for the program's life: other threads may hold it
    mod->folder = folder;
    mod->name = folder.substr(folder.find_last_of('\\') + 1);
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
    mod->map.layers = {mod->layer};
    mod->map.saves = Joined(Joined(game, kSaves), mod->name);
    g_mod.store(mod, std::memory_order_release);
    return true;
}

std::string ModFolder() {
    const Mod* mod = CurrentMod();
    return mod ? mod->folder : std::string();
}

} // namespace platform
