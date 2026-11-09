// platform.h's folder calls on std::filesystem, for every system: each path through SystemPath, then the backend's
// NativePath (seams.h), a path through a .zip read from the zip (zip_folders.h); and the folder search, its layers'
// searches (SystemSearches) merged. The file objects, one folder's search and the whole files are each backend's.
#include "platform/platform.h"

#include "lib/text_case.h"
#include "platform/path_layers.h"
#include "platform/seams.h"
#include "platform/zip_folders.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <system_error>

namespace platform {

namespace {

namespace fs = std::filesystem;

fs::path Native(std::string_view path, PathUse use) { return NativePath(SystemPath(path, use)); }

} // namespace

bool CreateFolder(std::string_view path) {
    std::error_code error;
    return fs::create_directory(Native(path, PathUse::Write), error);
}

bool RemoveFile(std::string_view path) {
    const fs::path file = Native(path, PathUse::Write);
    std::error_code error;
    return !fs::is_directory(file, error) && fs::remove(file, error);
}

bool RemoveFolder(std::string_view path) {
    const fs::path folder = Native(path, PathUse::Write);
    std::error_code error;
    return fs::is_directory(folder, error) && fs::remove(folder, error);
}

// As MoveFile: an existing target is kept, and the move fails.
bool RenameFile(std::string_view from, std::string_view to) {
    const fs::path target = Native(to, PathUse::Write);
    std::error_code error;
    if (fs::exists(target, error)) return false;
    fs::rename(Native(from, PathUse::Update), target, error);
    return !error;
}

bool CopyFileOver(std::string_view from, std::string_view to) {
    return CopySystemFile(SystemPath(from, PathUse::Read), SystemPath(to, PathUse::Write), true);
}

// A zip's files and folders too (zip_folders.h).
bool PathExists(std::string_view path) {
    const std::string mapped = SystemPath(path, PathUse::Read);
    ZipPlace place;
    if (InZip(mapped, &place)) return place.found;
    std::error_code error;
    return fs::exists(NativePath(mapped), error);
}

bool IsFile(std::string_view path) {
    const std::string mapped = SystemPath(path, PathUse::Read);
    ZipPlace place;
    if (InZip(mapped, &place)) return place.found && !place.folder;
    std::error_code error;
    const fs::file_status status = fs::status(NativePath(mapped), error);
    return fs::exists(status) && !fs::is_directory(status);
}

double FreeDiskSpace(std::string_view path) {
    std::error_code error;
    const fs::space_info space = fs::space(Native(path, PathUse::Read), error);
    return error ? 0.0 : static_cast<double>(space.available);
}

// One search, as the system's, when the path has no layers. With several a name an upper layer found hides the same
// name (Windows' case rules) below it, and when more than one layer found names they are ordered by name as NTFS
// orders them.
std::vector<FolderEntry> FolderEntries(std::string_view pattern) {
    const std::vector<FolderSearch> searches = SystemSearches(pattern);
    if (searches.size() == 1 && !searches.front().keeps) return SearchFolder(searches.front().pattern, pattern);
    std::vector<FolderEntry> entries;
    std::set<std::string> names;   // lower case
    std::size_t searchesFound = 0;
    for (const FolderSearch& search : searches) {
        bool found = false;
        for (FolderEntry& entry : SearchFolder(search.pattern, pattern)) {
            if (search.keeps && !search.keeps(entry.name)) continue;
            std::string lower = entry.name;
            text::LowerInPlace(lower.data(), lower.size());
            if (!names.insert(std::move(lower)).second) continue;
            entries.push_back(std::move(entry));
            found = true;
        }
        if (found) ++searchesFound;
    }
    if (searchesFound > 1)
        std::stable_sort(entries.begin(), entries.end(), [](const FolderEntry& a, const FolderEntry& b) {
            return text::CompareIgnoringCase(a.name, b.name) < 0;
        });
    return entries;
}

namespace {

// The files below the system's `folder` into `files`, a sub-folder's named "<sub>\<name>" (`sub` the folders above);
// of the folder's own entries only those `keeps` keeps.
void AddLayerFiles(const std::string& folder, const std::string& sub,
                   const std::function<bool(std::string_view)>& keeps, std::vector<LayerFile>* files) {
    const std::string pattern = folder + "\\*";
    for (FolderEntry& entry : SearchFolder(pattern, pattern)) {
        if (entry.IsDots() || (keeps && !keeps(entry.name))) continue;
        if (entry.folder)
            AddLayerFiles(folder + "\\" + entry.name, sub + entry.name + "\\", {}, files);
        else
            files->push_back({sub + entry.name, std::move(entry.path)});
    }
}

} // namespace

std::vector<FolderLayer> FilesByLayer(std::string_view folder) {
    std::vector<FolderLayer> layers;
    for (const FolderSearch& search : SystemSearches(std::string(folder) + "\\*")) {
        FolderLayer& layer = layers.emplace_back();
        layer.folder = FullGamePath(std::string_view(search.pattern).substr(0, search.pattern.find_last_of("\\/")));
        AddLayerFiles(layer.folder, "", search.keeps, &layer.files);
    }
    return layers;
}

} // namespace platform
