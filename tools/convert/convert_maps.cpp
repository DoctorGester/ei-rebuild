// ei-convert's maps: convert_maps.h. The folder's layout: docs/notes/mods.md, "Maps as folders".
#include "convert_maps.h"

#include "engine/data/resource.h"
#include "engine/data/map_folder.h"

#include "lib/bytes.h"
#include "lib/text_case.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <format>
#include <map>
#include <optional>

namespace convert_maps {

namespace {

// The bytes no loader reads, which the folder does not hold (map_folder.h): of each material of a map file its warp
// speed and reserved bytes; of a sector file the water vertices' displacements. A sector's flag byte 1 reads as 3.
constexpr std::size_t kMaterialUnreadFrom = offsetof(terrain::MapMaterial, warpSpeed);
constexpr std::size_t kSectorHeaderBytes = 5;
constexpr std::size_t kSectorLayerBytes = 33 * 33 * 8;
constexpr std::size_t kVertexBytes = 8;
constexpr std::uint8_t kWaterLayers = 3;

std::string ReadMapFile(std::string bytes) {
    if (bytes.size() < sizeof(terrain::MapHeader)) return bytes;
    const terrain::MapHeader header = lib::Load<terrain::MapHeader>(bytes.data());
    for (std::size_t i = 0; i < header.materials; ++i) {
        const std::size_t at = sizeof(terrain::MapHeader) + i * sizeof(terrain::MapMaterial);
        if (at + sizeof(terrain::MapMaterial) > bytes.size()) break;
        std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at + kMaterialUnreadFrom),
                  bytes.begin() + static_cast<std::ptrdiff_t>(at + sizeof(terrain::MapMaterial)), '\0');
    }
    return bytes;
}

std::string ReadSectorFile(std::string bytes) {
    if (bytes.size() < kSectorHeaderBytes + 2 * kSectorLayerBytes || (bytes[4] & 1) == 0) return bytes;
    bytes[4] = static_cast<char>(kWaterLayers);
    for (std::size_t at = kSectorHeaderBytes + kSectorLayerBytes; at < kSectorHeaderBytes + 2 * kSectorLayerBytes;
         at += kVertexBytes)
        bytes[at] = bytes[at + 1] = '\0';
    return bytes;
}

// The first byte where `made` differs from what the loaders read of `original`, as a line; empty when none.
std::string Difference(const std::string& what, const std::string& original, const std::string& made) {
    if (original == made) return {};
    if (original.size() != made.size()) return std::format("{}: {} bytes, made {}", what, original.size(), made.size());
    const auto at = std::mismatch(original.begin(), original.end(), made.begin()).first - original.begin();
    return std::format("{}: byte {} differs", what, at);
}

struct Measures {
    int maps = 0;
    int sectors = 0;
    int waterSectors = 0;
    long long seams = 0;
    long long folderBytes = 0;
    long long originalBytes = 0;
    double assembleSeconds = 0;
    double worstSeconds = 0;
    std::string worstMap;
};
Measures g_measures;

}   // namespace

Converted RoundTrip(MappedResource& archive, const std::string& folderName) {
    Converted out;
    std::vector<std::string> maps;
    archive.ListFiles(&maps, "*.mp");
    if (maps.empty()) {
        out.differences.push_back("no map file");
        return out;
    }
    const std::string name = maps[0].substr(0, maps[0].find('.'));
    auto file = [&](const std::string& entry) -> std::optional<std::string> {
        std::uint32_t size = 0;
        void* data = archive.GetFile(entry, &size);
        if (!data) return std::nullopt;
        std::string bytes(static_cast<const char*>(data), size);
        archive.CloseFile(data);
        return bytes;
    };
    const std::optional<std::string> mp = file(maps[0]);
    std::map<std::pair<int, int>, std::string> sectors;
    std::string error;
    const std::optional<map_folder::MapData> a = map_folder::Decode(
        name, mp ? *mp : std::string(),
        [&](int sx, int sy) {
            std::optional<std::string> bytes = file(map_folder::SectorName(name, sx, sy));
            if (bytes) sectors[{sx, sy}] = *bytes;
            return bytes;
        },
        &error);
    if (!a) {
        out.differences.push_back(error);
        return out;
    }
    // The originals by the names the archive lists (a system that tells case apart removes them by those).
    std::vector<std::string> listed;
    archive.ListFiles(&listed, nullptr);
    auto listedName = [&](const std::string& made) {
        const auto found =
            std::ranges::find_if(listed, [&](const std::string& n) { return text::CompareIgnoringCase(n, made) == 0; });
        return found == listed.end() ? made : *found;
    };
    out.originals.push_back(maps[0]);
    for (const auto& [at, bytes] : sectors)
        out.originals.push_back(listedName(map_folder::SectorName(name, at.first, at.second)));
    out.files = map_folder::WriteFolder(*a);

    // Read back as the game reads a converted folder: its files the only place.
    map_folder::Place place;
    place.has = [&](std::string_view entry) {
        return std::ranges::any_of(out.files, [&](const auto& f) { return f.first == entry; });
    };
    place.read = [&](std::string_view entry, std::string* bytes) {
        for (const auto& [fileName, fileBytes] : out.files)
            if (fileName == entry) {
                *bytes = fileBytes;
                return true;
            }
        return false;
    };
    std::vector<std::string> warnings;
    const auto start = std::chrono::steady_clock::now();
    const std::optional<map_folder::MapData> b = map_folder::Assemble({place}, folderName, &warnings);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (const std::string& line : warnings) out.differences.push_back("assembling: " + line);
    if (!b) {
        out.differences.push_back("the folder makes no map");
        return out;
    }
    if (b->name != a->name) out.differences.push_back("the name " + b->name + ", not " + a->name);
    if (std::string line = Difference(maps[0], ReadMapFile(*mp), map_folder::EncodeMap(*b)); !line.empty())
        out.differences.push_back(line);
    for (const auto& [at, bytes] : sectors) {
        const std::string sectorName = map_folder::SectorName(name, at.first, at.second);
        if (std::string line =
                Difference(sectorName, ReadSectorFile(bytes), map_folder::EncodeSector(*b, at.first, at.second));
            !line.empty())
            out.differences.push_back(line);
    }

    Measures& m = g_measures;
    ++m.maps;
    m.sectors += static_cast<int>(sectors.size());
    m.waterSectors += static_cast<int>(std::ranges::count(a->water, std::uint8_t{1}));
    m.seams += static_cast<long long>(a->seams.size());
    m.originalBytes += static_cast<long long>(mp->size());
    for (const auto& [at, bytes] : sectors) m.originalBytes += static_cast<long long>(bytes.size());
    for (const auto& [fileName, bytes] : out.files) m.folderBytes += static_cast<long long>(bytes.size());
    m.assembleSeconds += seconds;
    if (seconds > m.worstSeconds) {
        m.worstSeconds = seconds;
        m.worstMap = folderName;
    }
    return out;
}

void PrintMeasures() {
    const Measures& m = g_measures;
    if (m.maps == 0) return;
    std::printf("ei-convert: maps: %d maps, %d sectors (%d with water), %lld seams; %.1f MB as files, %.1f MB as "
                "folders; assembled from the folders in %.2f s (the slowest %s, %.3f s)\n",
                m.maps, m.sectors, m.waterSectors, m.seams, m.originalBytes / 1e6, m.folderBytes / 1e6,
                m.assembleSeconds, m.worstMap.c_str(), m.worstSeconds);
}

}   // namespace convert_maps
