// game/map/map_folder.h: maps as folders and back (docs/notes/mods.md, "Maps as folders"), and lib/png.h's writer. A
// made-up map of 2 x 2 sectors (one with water, seams on its shared edges) comes back byte for byte through its files
// and through its folder; a heights.png over the original replaces the heights alone and brings normals worked out
// from them; every 16-bit sample comes back through a PNG.
#include "game/map/map_folder.h"

#include "stb/stb_image.h"

#include "lib/png.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::printf("ei_unit_check: map_folder: %s\n", what.c_str());
}

// A map of 2 x 2 sectors with every layer drawn at random: sector (1, 0) has water, a few shared vertices are seams.
map_folder::MapData MadeUpMap() {
    std::mt19937 random(8);
    map_folder::MapData map;
    map.name = "mf8";
    map.header.magic = terrain::kMapMagic;
    map.header.heightRange = 21.5f;
    map.header.sectorsX = 2;
    map.header.sectorsY = 2;
    map.header.pages = 2;
    map.header.pageSize = 512;
    map.header.tileSize = 64;
    map.materials.resize(2);
    map.materials[1].kind = 3;
    map.materials[1].colour = {0.25f, 0.5f, 0.75f};
    map.materials[1].opacity = 0.6f;
    map.materials[1].light = 1.5f;
    map.materials[1].priority = 2.0f;
    for (int i = 0; i < 128; ++i) map.classes.push_back(static_cast<std::int32_t>(random() % 16));
    map.animated = {{5, 4}, {9, 8}};
    map.Fit();
    map.water[1] = 1;
    for (map_folder::Vertex& v : map.ground) {
        v.offsetX = static_cast<std::int8_t>(random() % 101 - 50);
        v.offsetY = static_cast<std::int8_t>(random() % 101 - 50);
        v.height = static_cast<std::uint16_t>(random());
        v.normal = static_cast<std::uint32_t>(random());
    }
    for (map_folder::Vertex& v : map.waterVertices) {
        v.height = static_cast<std::uint16_t>(random());
        v.normal = static_cast<std::uint32_t>(random());
    }
    for (std::uint16_t& word : map.tiles) word = static_cast<std::uint16_t>(random() % 128 | (random() % 4) << 14);
    for (int row = 0; row < 16; ++row)
        for (int column = 16; column < 32; ++column) {
            const std::size_t at = static_cast<std::size_t>(row * map.TileColumns() + column);
            map.waterMaterials[at] =
                random() % 3 == 0 ? map_folder::MapData::kNone : static_cast<std::uint16_t>(random() % 2);
            map.waterTiles[at] = static_cast<std::uint16_t>(random() % 128 | (random() % 4) << 14);
        }
    map_folder::Seam seam;
    seam.sx = 1;
    seam.row = 7;
    seam.height = -3;
    seam.offsetX = 2;
    seam.normal = {1, -1, 0};
    map.seams.push_back(seam);
    seam = {};
    seam.sy = 1;
    seam.column = 12;
    seam.height = 9;
    map.seams.push_back(seam);
    seam = {};
    seam.sx = 1;
    seam.sy = 1;
    seam.normal = {0, 0, 2};
    map.seams.push_back(seam);
    // The water grid's vertices of column 32 are sector (1, 0)'s first column (sector (0, 0) has none): no seam there.
    return map;
}

std::map<std::string, std::string> FilesOf(const map_folder::MapData& map) {
    std::map<std::string, std::string> files;
    files[map.name + ".MP"] = map_folder::EncodeMap(map);
    for (int sy = 0; sy < map.header.sectorsY; ++sy)
        for (int sx = 0; sx < map.header.sectorsX; ++sx)
            files[map_folder::SectorName(map.name, sx, sy)] = map_folder::EncodeSector(map, sx, sy);
    return files;
}

map_folder::Place PlaceOf(const std::map<std::string, std::string>& files, const std::string& mapFile) {
    map_folder::Place place;
    place.has = [&files](std::string_view name) { return files.contains(std::string(name)); };
    place.read = [&files](std::string_view name, std::string* bytes) {
        const auto found = files.find(std::string(name));
        if (found == files.end()) return false;
        *bytes = found->second;
        return true;
    };
    place.mapFile = mapFile;
    return place;
}

void CheckRoundTrips() {
    const map_folder::MapData map = MadeUpMap();
    const std::map<std::string, std::string> files = FilesOf(map);
    std::string error;
    const std::optional<map_folder::MapData> decoded = map_folder::Decode(
        map.name, files.at("mf8.MP"),
        [&](int sx, int sy) { return std::optional<std::string>(files.at(map_folder::SectorName(map.name, sx, sy))); },
        &error);
    Check(decoded.has_value(), "the made-up map does not decode: " + error);
    if (!decoded) return;
    Check(FilesOf(*decoded) == files, "the decoded map's files differ");
    Check(decoded->seams.size() == map.seams.size(),
          "the decoded map has " + std::to_string(decoded->seams.size()) + " seams");

    // The folder's files as the only place, as a converted folder.
    std::map<std::string, std::string> folder;
    for (const auto& [name, bytes] : map_folder::WriteFolder(*decoded)) folder[name] = bytes;
    std::vector<std::string> warnings;
    const std::optional<map_folder::MapData> assembled =
        map_folder::Assemble({PlaceOf(folder, "")}, "other", &warnings);
    Check(warnings.empty(), "assembling the folder warns: " + (warnings.empty() ? std::string() : warnings[0]));
    Check(assembled.has_value() && FilesOf(*assembled) == files, "the folder's map's files differ");

    // No file of the folder: no map (the originals are read as they are).
    Check(!map_folder::Assemble({PlaceOf(files, "mf8.MP")}, "mf8", &warnings), "the originals alone make a map");

    // heights.png over the original: the heights replaced, the normals worked out from them, the rest the original's
    // but the ground seams' heights and normals (of the replaced values).
    map_folder::MapData higher = *decoded;
    for (map_folder::Vertex& v : higher.ground) v.height = static_cast<std::uint16_t>(v.height / 2);
    std::map<std::string, std::string> over;
    for (const auto& [name, bytes] : map_folder::WriteFolder(higher))
        if (name == "heights.png") over[name] = bytes;
    const std::optional<map_folder::MapData> mixed =
        map_folder::Assemble({PlaceOf(over, ""), PlaceOf(files, "mf8.MP")}, "mf8", &warnings);
    Check(mixed.has_value(), "heights.png over the original makes no map");
    if (!mixed) return;
    map_folder::MapData expected = higher;
    map_folder::WorkOutNormals(expected, false);
    std::vector<map_folder::Seam> seams;
    for (map_folder::Seam seam : decoded->seams) {
        if (!seam.water) {
            seam.height = 0;
            seam.normal = {};
        }
        if (seam.height != 0 || seam.offsetX != 0 || seam.offsetY != 0 || seam.normal != std::array<int, 3>{})
            seams.push_back(seam);
    }
    Check(!seams.empty() && seams.size() < decoded->seams.size(), "the made-up map's seams do not test the overlay");
    bool same = mixed->ground.size() == expected.ground.size() && mixed->tiles == decoded->tiles &&
                mixed->waterVertices == decoded->waterVertices && mixed->seams == seams;
    for (std::size_t i = 0; same && i < expected.ground.size(); ++i)
        same = mixed->ground[i].height == expected.ground[i].height &&
               mixed->ground[i].normal == expected.ground[i].normal &&
               mixed->ground[i].offsetX == decoded->ground[i].offsetX;
    Check(same,
          "heights.png over the original: not its heights with worked out normals over the original's other layers");

    // A flat map's worked out normals point up.
    map_folder::MapData flat = *decoded;
    for (map_folder::Vertex& v : flat.ground) v = {};
    map_folder::WorkOutNormals(flat, false);
    Check(flat.ground[100].normal == map_folder::Vertex::kUp, "a flat map's normal is not up");
}

void CheckPng() {
    std::vector<std::uint16_t> samples(256 * 256 * 2);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<std::uint16_t>(i * 7);
    const std::string png = png::Write(256, 512, 1, 16, samples);
    int width = 0, height = 0, components = 0;
    stbi_us* read = stbi_load_16_from_memory(reinterpret_cast<const stbi_uc*>(png.data()), static_cast<int>(png.size()),
                                             &width, &height, &components, 1);
    Check(read && width == 256 && height == 512 && std::equal(samples.begin(), samples.end(), read),
          "a 16-bit grey PNG");
    stbi_image_free(read);
    std::vector<std::uint16_t> rgb(33 * 17 * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<std::uint16_t>(i * 31 % 256);
    const std::string png8 = png::Write(33, 17, 3, 8, rgb);
    stbi_uc* read8 = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png8.data()), static_cast<int>(png8.size()),
                                           &width, &height, &components, 3);
    Check(read8 && width == 33 && height == 17 && std::equal(rgb.begin(), rgb.end(), read8), "an 8-bit RGB PNG");
    stbi_image_free(read8);
    Check(png::Write(2, 2, 5, 8, samples).empty() && png::Write(2, 2, 1, 12, samples).empty(),
          "a PNG of a kind the writer has not");
}

}   // namespace

int RunMapFolderChecks() {
    CheckPng();
    CheckRoundTrips();
    return g_failures;
}
