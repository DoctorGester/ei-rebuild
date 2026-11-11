// game/data/map_folder.h: maps as folders and back (docs/notes/mods.md, "Maps as folders"), and lib/png.h's writer. A
// made-up map of 2 x 2 sectors (one with water, seams on its shared edges) comes back byte for byte through its files
// and through its folder; a heights.png over the original replaces the heights alone and brings normals worked out
// from them; every 16-bit sample comes back through a PNG; lib/png.h's reader gives the pixels the writer was given for
// every kind of PNG it makes (in segments too, at 8 and 16 bits) and fixed ones for a palette with transparency.
#include "game/data/map_folder.h"

#include "lib/png.h"

#include "miniz/miniz.h"

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
    int width = 0, height = 0;
    std::vector<std::uint16_t> read;
    std::string error;
    // A grey PNG reads as RGBA with the grey in R, G and B.
    bool same = png::ReadRgba16(png, &width, &height, &read, &error) && width == 256 && height == 512 &&
                read.size() == samples.size() * 4;
    for (std::size_t i = 0; same && i < samples.size(); ++i)
        same = read[4 * i] == samples[i] && read[4 * i + 1] == samples[i] && read[4 * i + 2] == samples[i] &&
               read[4 * i + 3] == 65535;
    Check(same, "a 16-bit grey PNG");
    std::vector<std::uint16_t> rgb(33 * 17 * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<std::uint16_t>(i * 31 % 256);
    const std::string png8 = png::Write(33, 17, 3, 8, rgb);
    std::vector<std::uint8_t> read8;
    same = png::ReadRgba(png8, &width, &height, &read8, &error) && width == 33 && height == 17 && read8.size() == 33 * 17 * 4;
    for (std::size_t i = 0; same && i < 33 * 17; ++i)
        same = read8[4 * i] == rgb[3 * i] && read8[4 * i + 1] == rgb[3 * i + 1] && read8[4 * i + 2] == rgb[3 * i + 2] &&
               read8[4 * i + 3] == 255;
    Check(same, "an 8-bit RGB PNG");
    // The same at 16 bits: a byte b is b * 257.
    same = png::ReadRgba16(png8, &width, &height, &read, &error) && read.size() == 33 * 17 * 4;
    for (std::size_t i = 0; same && i < 33 * 17; ++i)
        same = read[4 * i] == rgb[3 * i] * 257 && read[4 * i + 2] == rgb[3 * i + 2] * 257 && read[4 * i + 3] == 65535;
    Check(same, "an 8-bit RGB PNG at 16 bits: " + error);
    Check(png::Write(2, 2, 5, 8, samples).empty() && png::Write(2, 2, 1, 12, samples).empty(),
          "a PNG of a kind the writer has not");
}

// png::ReadRgba's pixels, or empty when it fails.
std::vector<std::uint8_t> ReadRgba(const std::string& png, int* width, int* height) {
    std::vector<std::uint8_t> rgba;
    std::string error;
    if (!png::ReadRgba(png, width, height, &rgba, &error)) rgba.clear();
    return rgba;
}

void PutBigEndian(std::string& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(v >> shift));
}

void PutChunk(std::string& out, const char* type, const std::string& data) {
    PutBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::string typed = std::string(type) + data;
    out += typed;
    PutBigEndian(out, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(typed.data()), typed.size())));
}

// An 8-bit palette PNG of 4 colours, the third half and the fourth fully transparent (tRNS), 5 x 3 pixels.
std::string PalettePng() {
    std::string header;
    PutBigEndian(header, 5);
    PutBigEndian(header, 3);
    header += std::string({8, 3, 0, 0, 0});   // 8 bits, a palette, deflate, the usual filters, not interlaced
    std::string rows;
    for (int y = 0; y < 3; ++y) {
        rows.push_back(0);   // no filter
        for (int x = 0; x < 5; ++x) rows.push_back(static_cast<char>((x + y) % 4));
    }
    mz_ulong packedSize = mz_compressBound(static_cast<mz_ulong>(rows.size()));
    std::vector<unsigned char> packed(packedSize);
    mz_compress(packed.data(), &packedSize, reinterpret_cast<const unsigned char*>(rows.data()), static_cast<mz_ulong>(rows.size()));
    std::string png = {static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n', 26, '\n'};
    PutChunk(png, "IHDR", header);
    const unsigned char palette[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 16, 32, 48};
    const unsigned char alpha[] = {255, 255, 128, 0};
    PutChunk(png, "PLTE", std::string(std::begin(palette), std::end(palette)));
    PutChunk(png, "tRNS", std::string(std::begin(alpha), std::end(alpha)));
    PutChunk(png, "IDAT", std::string(reinterpret_cast<const char*>(packed.data()), packedSize));
    PutChunk(png, "IEND", {});
    return png;
}

// The RGBA a PNG made from `samples` (`channels` of `bits`, 16-bit ones as 8 bits their high byte) must read as: grey
// repeated in R, G and B, no alpha opaque.
std::vector<std::uint8_t> ExpectedRgba(int pixels, int channels, int bits, const std::vector<std::uint16_t>& samples) {
    std::vector<std::uint8_t> out;
    for (int i = 0; i < pixels; ++i) {
        auto at = [&](int c) {
            return static_cast<std::uint8_t>(bits == 16 ? samples[i * channels + c] >> 8 : samples[i * channels + c]);
        };
        const bool colour = channels >= 3;
        out.push_back(at(0));
        out.push_back(at(colour ? 1 : 0));
        out.push_back(at(colour ? 2 : 0));
        out.push_back(channels == 2 ? at(1) : channels == 4 ? at(3) : 255);
    }
    return out;
}

void CheckPngRead() {
    std::vector<std::uint16_t> samples(19 * 7 * 4);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<std::uint16_t>(i * 2654435761u >> 7);
    struct Case {
        std::string png;
        std::vector<std::uint8_t> expected;
    };
    // The palette: 5 x 3 pixels of entry (x + y) % 4, the colours of PalettePng with the alphas 255, 255, 128, 0.
    const unsigned char colours[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 128}, {16, 32, 48, 0}};
    Case palette{PalettePng(), {}};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            palette.expected.insert(palette.expected.end(), colours[(x + y) % 4], colours[(x + y) % 4] + 4);
    std::vector<Case> cases = {palette};
    for (int channels = 1; channels <= 4; ++channels)
        for (int bits : {8, 16}) {
            std::vector<std::uint16_t> some(samples.begin(), samples.begin() + 19 * 7 * channels);
            if (bits == 8)
                for (std::uint16_t& v : some) v &= 0xFF;
            cases.push_back({png::Write(19, 7, channels, bits, some), ExpectedRgba(19 * 7, channels, bits, some)});
        }
    // Large enough for segments (lib/png.h): 8-bit RGB, 16-bit grey and alpha; and the first with its second
    // segment's start moved, which the reader then reads as a plain PNG.
    std::vector<std::uint8_t> rgb(301 * 1500 * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<std::uint8_t>((i * 2654435761u >> 13) % 7 + i % 301);
    cases.push_back({png::Write(301, 1500, 3, rgb),
                     ExpectedRgba(301 * 1500, 3, 8, std::vector<std::uint16_t>(rgb.begin(), rgb.end()))});
    std::vector<std::uint16_t> greyAlpha(97 * 2100 * 2);
    for (std::size_t i = 0; i < greyAlpha.size(); ++i) greyAlpha[i] = static_cast<std::uint16_t>(i * 40503u);
    cases.push_back({png::Write(97, 2100, 2, 16, greyAlpha), ExpectedRgba(97 * 2100, 2, 16, greyAlpha)});
    const std::size_t segmentsAt = cases[cases.size() - 2].png.find("eiSG");
    Check(segmentsAt != std::string::npos && cases.back().png.find("eiSG") != std::string::npos,
          "a large PNG has segments");
    Case moved = cases[cases.size() - 2];
    if (segmentsAt != std::string::npos) ++moved.png[segmentsAt + 4 + 4 + 4 + 3];
    cases.push_back(moved);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        int width = 0, height = 0;
        const std::vector<std::uint8_t> ours = ReadRgba(cases[i].png, &width, &height);
        Check(!ours.empty() && static_cast<std::size_t>(width) * height * 4 == ours.size() && ours == cases[i].expected,
              "png::ReadRgba, PNG " + std::to_string(i));
        // At 16 bits: the high byte of each sample is the 8-bit read's.
        std::vector<std::uint16_t> wide;
        std::string error;
        bool high = png::ReadRgba16(cases[i].png, &width, &height, &wide, &error) && wide.size() == ours.size();
        for (std::size_t k = 0; high && k < wide.size(); ++k) high = (wide[k] >> 8) == ours[k];
        Check(high, "png::ReadRgba16, PNG " + std::to_string(i) + ": " + error);
    }
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error;
    const std::string cut = cases[1].png.substr(0, cases[1].png.size() / 2);
    Check(!png::ReadRgba(cut, &width, &height, &rgba, &error) && !error.empty(), "a cut PNG reads");
    std::vector<std::uint16_t> wide;
    error.clear();
    Check(!png::ReadRgba16(cut, &width, &height, &wide, &error) && !error.empty(), "a cut PNG reads at 16 bits");
}

}   // namespace

int RunMapFolderChecks() {
    CheckPng();
    CheckPngRead();
    CheckRoundTrips();
    return g_failures;
}
