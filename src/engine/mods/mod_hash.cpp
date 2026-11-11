// A mod's content hash (mod_hash.h).
#include "engine/mods/mod_hash.h"

#include "lib/text_case.h"
#include "platform/path_layers.h"
#include "platform/platform.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace modhash {
namespace {

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;

void Mix(std::uint64_t* hash, std::string_view bytes) {
    for (const char c : bytes) {
        *hash ^= static_cast<std::uint8_t>(c);
        *hash *= kFnvPrime;
    }
}

void MixWord(std::uint64_t* hash, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value), static_cast<char>(value >> 8), static_cast<char>(value >> 16),
                           static_cast<char>(value >> 24)};
    Mix(hash, std::string_view(bytes, sizeof(bytes)));
}

bool StartsWith(std::string_view text, std::string_view start) { return text.substr(0, start.size()) == start; }
bool EndsWith(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

// Whether Res\'s entry `name` (an archive or its folder) is a counted one: the databases, the texts and the data
// effects (their [server] blocks decide the effects' world objects) and the spell scripts.
bool CountedResource(std::string_view name) {
    return StartsWith(name, "database") || StartsWith(name, "texts") || StartsWith(name, "effects") ||
           StartsWith(name, "spells");
}

// Whether the folder at `relative` (lower case) may hold a counted file: the walk goes into no other.
bool MayHold(std::string_view relative) {
    if (relative == "maps" || StartsWith(relative, "maps\\") || relative == "config" || relative == "res") return true;
    return StartsWith(relative, "res\\") && CountedResource(relative.substr(4));
}

struct File {
    std::string relative;   // lower case
    std::string path;
    std::uint32_t size = 0;
};

void Walk(const std::string& folder, const std::string& relative, std::vector<File>* files) {
    const std::string pattern = folder + "\\*";
    for (const platform::FolderEntry& entry : platform::SearchFolder(pattern, pattern)) {
        if (entry.IsDots()) continue;
        std::string name = entry.name;
        text::LowerInPlace(name.data(), name.size());
        std::string below = relative.empty() ? name : relative + "\\" + name;
        if (entry.folder) {
            if (MayHold(below)) Walk(folder + "\\" + entry.name, below, files);
        } else if (Counted(below)) {
            files->push_back({std::move(below), folder + "\\" + entry.name, entry.size});
        }
    }
}

} // namespace

bool Counted(std::string_view relative) {
    if (relative == "config.reg" || relative == "config.reg.toml") return true;
    if (StartsWith(relative, "maps\\")) return true;
    if (StartsWith(relative, "config\\")) {
        const bool inside = relative.find('\\', 7) == std::string_view::npos;   // not in a folder below Config
        return inside && (EndsWith(relative, ".reg") || EndsWith(relative, ".reg.toml"));
    }
    if (!StartsWith(relative, "res\\")) return false;
    return CountedResource(relative.substr(4));
}

std::uint64_t ContentHash(const std::string& root, Count* count) {
    std::vector<File> files;
    std::string folder = root;
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    Walk(folder, "", &files);
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.relative < b.relative; });
    std::uint64_t hash = kFnvOffset;
    Count counted;
    for (const File& file : files) {
        std::uint32_t crc = 0;   // 0 when the file fails to read
        platform::LayerFileCrc32(file.path, &crc);
        Mix(&hash, file.relative);
        Mix(&hash, std::string_view("", 1));
        MixWord(&hash, file.size);
        MixWord(&hash, crc);
        ++counted.files;
        counted.bytes += file.size;
    }
    if (count) *count = counted;
    return hash;
}

} // namespace modhash
