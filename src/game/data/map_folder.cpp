// Maps as folders: map_folder.h. The folder's files, units and grids: docs/notes/mods.md, "Maps as folders".
#include "game/data/map_folder.h"

#include "game/core/asset_text.h"
#include "game/data/record_names.h"

#include "lib/bytes.h"
#include "lib/enum_array.h"
#include "lib/png.h"
#include "lib/rounding.h"
#include "lib/toml.h"
#include "lib/vecmath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <tuple>

namespace map_folder {
namespace {

using terrain::MapAnimatedTexture;
using terrain::MapHeader;
using terrain::MapMaterial;

// The sector files (terrain_sector.h names the same sizes for the landscape, ai_map_load.cpp for the AI map).
constexpr int kSectorCells = 32;               // cells along a sector's side
constexpr int kSectorRow = kSectorCells + 1;   // vertices along it
constexpr int kSectorVertices = kSectorRow * kSectorRow;
constexpr int kSectorTiles = 16;   // tiles along it
constexpr int kLayerTiles = kSectorTiles * kSectorTiles;
constexpr int kMostSectors = 999;   // along a side: three digits in the sector files' names
constexpr std::uint32_t kSectorMagic = 0xCF4BF774;
constexpr std::size_t kSectorHeaderBytes = 5;   // the magic and the flag byte
constexpr std::size_t kVertexBytes = 8;
constexpr std::size_t kLayerBytes = kSectorVertices * kVertexBytes;
constexpr std::size_t kTileLayerBytes = kLayerTiles * 2;
// The flag byte: bit 0 water vertices and a water material per tile, bit 1 water tiles (bit 0 brings them too). No
// shipped sector has bit 1 alone, which the landscape cannot read.
constexpr std::uint8_t kWaterVertices = 1;
constexpr std::uint8_t kWaterLayers = 3;
// The terrain's tile counts hold both layers' pages and the two extra pages: kMaxPages (terrain_sector.h) / 2 - 2.
constexpr int kMostPages = 126;

// The packed normal (terrain_sector.cpp reads it): z in the top 10 bits, x and y in 11 bits each, the code less 1000
// in thousandths.
constexpr int kNormalZShift = 22;
constexpr int kNormalXShift = 11;
constexpr std::uint32_t kNormalXYMask = 0x7FF;
constexpr std::uint32_t kNormalZMask = 0x3FF;
constexpr float kNormalUnits = 1000.0f;
constexpr int kNormalBias = 1000;
// A displacement step is half of 1/126 cell (terrain_sector.cpp's kOffsetScale).
constexpr float kOffsetUnit = 0.5f / 126.0f;
constexpr float kHeightUnits = 65534.0f;   // a height of this many units is the map's height range

// The layers' pictures: offsets.png's 128 is no displacement; tiles.png's blue is the quarter turns times kTurnLevel.
constexpr int kOffsetZero = 128;
constexpr int kTurnLevel = 85;
constexpr std::uint16_t kTextureMask = 0x3FFF;
constexpr int kTurnShift = 14;
constexpr std::uint8_t kNoMaterialLevel = 255;   // water_materials.png's none
constexpr char kWaterSector = 'W';
constexpr char kDrySector = '.';

constexpr lib::EnumArray<std::string_view, Layer, static_cast<std::size_t>(Layer::Count)> kFileNames = {
    {"map.toml", "heights.png", "offsets.png", "normals.png", "water.png", "water_normals.png", "tiles.png",
     "water_tiles.png", "water_materials.png", "seams.toml"}};

std::array<int, 3> NormalCodes(std::uint32_t normal) {
    return {static_cast<int>((normal >> kNormalXShift) & kNormalXYMask), static_cast<int>(normal & kNormalXYMask),
            static_cast<int>(normal >> kNormalZShift)};
}

std::uint32_t PackNormal(const std::array<int, 3>& codes) {
    return (static_cast<std::uint32_t>(codes[2]) & kNormalZMask) << kNormalZShift |
           (static_cast<std::uint32_t>(codes[0]) & kNormalXYMask) << kNormalXShift |
           (static_cast<std::uint32_t>(codes[1]) & kNormalXYMask);
}

// The copies of vertex column (or row) `v` among the sectors, the earlier first: a sector and the vertex's place in it.
// Where two sectors meet, the vertex is the earlier one's last and the later one's first.
struct Copy {
    int sector = 0, at = 0;
};
int CopiesOf(int v, int sectors, Copy (&copies)[2]) {
    int count = 0;
    if (v % kSectorCells == 0 && v > 0) copies[count++] = {v / kSectorCells - 1, kSectorCells};
    if (v / kSectorCells < sectors) copies[count++] = {v / kSectorCells, v % kSectorCells};
    return count;
}

std::size_t SectorIndex(const MapData& map, int sx, int sy) {
    return static_cast<std::size_t>(sy * map.header.sectorsX + sx);
}

// The grid's index of vertex (column, row) of sector (sx, sy), and of tile (column, row) of it.
std::size_t VertexIndex(const MapData& map, int sx, int sy, int column, int row) {
    return static_cast<std::size_t>((sy * kSectorCells + row) * map.VertexColumns() + sx * kSectorCells + column);
}
std::size_t TileIndex(const MapData& map, int sx, int sy, int column, int row) {
    return static_cast<std::size_t>((sy * kSectorTiles + row) * map.TileColumns() + sx * kSectorTiles + column);
}

// A sector file's parts.
struct SectorFile {
    std::string_view bytes;
    bool water = false;
    Vertex VertexAt(int layer, int index) const {
        const char* at = bytes.data() + kSectorHeaderBytes + static_cast<std::size_t>(layer) * kLayerBytes +
                         static_cast<std::size_t>(index) * kVertexBytes;
        return {lib::Load<std::int8_t>(at), lib::Load<std::int8_t>(at + 1), lib::Load<std::uint16_t>(at + 2),
                lib::Load<std::uint32_t>(at + 4)};
    }
    // Tile layer 0 the ground's, 1 the water tiles, 2 the water materials.
    std::uint16_t WordAt(int tileLayer, int index) const {
        const std::size_t start = kSectorHeaderBytes + (water ? 2 : 1) * kLayerBytes;
        return lib::Load<std::uint16_t>(bytes.data() + start + static_cast<std::size_t>(tileLayer) * kTileLayerBytes +
                                        static_cast<std::size_t>(index) * 2);
    }
};

std::size_t SectorBytes(bool water) {
    return kSectorHeaderBytes + (water ? 2 * kLayerBytes + 3 * kTileLayerBytes : kLayerBytes + kTileLayerBytes);
}

// A seam's sector, vertex and layer, for its lookup.
using SeamKey = std::tuple<int, int, bool>;

// The vertex a sector holds: the grid's with the seam's differences added.
Vertex WithSeam(Vertex vertex, const Seam* seam) {
    if (!seam) return vertex;
    vertex.height = static_cast<std::uint16_t>(vertex.height + seam->height);
    vertex.offsetX = static_cast<std::int8_t>(vertex.offsetX + seam->offsetX);
    vertex.offsetY = static_cast<std::int8_t>(vertex.offsetY + seam->offsetY);
    std::array<int, 3> codes = NormalCodes(vertex.normal);
    for (int i = 0; i < 3; ++i) codes[i] += seam->normal[i];
    vertex.normal = PackNormal(codes);
    return vertex;
}

// The seam of a sector's copy `own` against the grid's `grid` (nullopt where they are the same).
std::optional<Seam> SeamOf(const Vertex& own, const Vertex& grid, int sx, int sy, int column, int row, bool water) {
    Seam seam{sx, sy, column, row, water};
    seam.height = own.height - grid.height;
    if (!water) {
        seam.offsetX = own.offsetX - grid.offsetX;
        seam.offsetY = own.offsetY - grid.offsetY;
    }
    const std::array<int, 3> a = NormalCodes(own.normal), b = NormalCodes(grid.normal);
    for (int i = 0; i < 3; ++i) seam.normal[i] = a[i] - b[i];
    if (seam.height == 0 && seam.offsetX == 0 && seam.offsetY == 0 && seam.normal == std::array<int, 3>{})
        return std::nullopt;
    return seam;
}

// ---- The header (map.toml) ---------------------------------------------------------------------------------------

std::string IntArray(std::initializer_list<int> values) {
    std::vector<std::string> items;
    for (int value : values) items.push_back(toml::Int(value));
    return toml::Array(items);
}

// A water material's kind (MapMaterial::kind) by what reads it: the AI map counts the cells under kinds 2 and 3 as
// under water (ai_map_load.cpp), kind 4's vertices keep the ground's x and y and have no waves (TerrainMaterial::
// kKeepsGroundXY); every other kind is dry to the AI and waves. The files have 1 and 3; 0 and 2 (as 1 and 3) stay
// numbers.
constexpr std::pair<int, std::string_view> kMaterialKinds[] = {{1, "dry"}, {3, "water"}, {4, "still"}};

std::string MaterialKindText(int kind) {
    for (const auto& [value, name] : kMaterialKinds)
        if (value == kind) return toml::String(name);
    return toml::Int(kind);
}
bool ReadMaterialKind(const toml::Value& value, int* kind) {
    if (!value.IsString()) return toml::Read(value, kind);
    for (const auto& [code, name] : kMaterialKinds)
        if (name == value.String()) {
            *kind = code;
            return true;
        }
    return false;
}

// A tile texture's class by name: the ground's surface (record_names.h), 16 a line; another as its number.
constexpr int kLineClass = 16;
std::string ClassText(std::int32_t value) {
    if (value == kLineClass) return toml::String("line");
    if (value >= 0 && value < std::ssize(recordnames::kSurfaces))
        return toml::String(recordnames::kSurfaces[static_cast<std::size_t>(value)]);
    return toml::Int(value);
}
bool ReadClass(const toml::Value& value, std::int32_t* out) {
    if (!value.IsString()) return toml::Read(value, out);
    if (value.String() == "line") {
        *out = kLineClass;
        return true;
    }
    for (std::size_t i = 0; i < std::size(recordnames::kSurfaces); ++i)
        if (recordnames::kSurfaces[i] == value.String()) {
            *out = static_cast<std::int32_t>(i);
            return true;
        }
    return false;
}

std::string HeaderToml(const MapData& map) {
    const MapHeader& h = map.header;
    toml::Writer w;
    w.Comment("A map (docs/notes/mods.md, \"Maps as folders\"): this header and the PNG layers beside it. Heights are");
    w.Comment("in 1/65534 of height_range, in cells (a cell is the vertices' spacing; a sector is 32 x 32 cells).");
    w.Pair("name", toml::String(text::FromAssetText(map.name)));
    w.Pair("height_range", toml::Float(h.heightRange));
    w.Pair("sectors", IntArray({h.sectorsX, h.sectorsY}));
    w.Pair("pages", toml::Int(h.pages));
    w.Pair("page_size", toml::Int(h.pageSize));
    w.Pair("tile_size", toml::Int(h.tileSize));
    w.Comment("A text per row of sectors, the first (y = 0) first; a character per sector: W with water, . without.");
    std::vector<std::string> rows;
    for (int sy = 0; sy < h.sectorsY; ++sy) {
        std::string row;
        for (int sx = 0; sx < h.sectorsX; ++sx) row += map.water[SectorIndex(map, sx, sy)] ? kWaterSector : kDrySector;
        rows.push_back(toml::String(row));
    }
    w.Pair("water_sectors", toml::Array(rows, true));
    w.Comment("Per tile texture, its class: the ground's surface (its sounds and prints), or line.");
    std::string classes = "[";
    for (std::size_t i = 0; i < map.classes.size(); ++i) {
        classes += i % 8 == 0 ? "\n    " : " ";
        classes += ClassText(map.classes[i]) + ",";
    }
    w.Pair("classes", classes + (map.classes.empty() ? "]" : "\n]"));
    std::vector<std::string> animated;
    for (const MapAnimatedTexture& entry : map.animated)
        animated.push_back(
            toml::InlineTable({{"texture", toml::Int(entry.texture)}, {"frames", toml::Int(entry.frames)}}));
    w.Pair("animated", toml::Array(animated, true));
    for (const MapMaterial& m : map.materials) {
        w.ArrayTable("material");
        w.Pair("kind", MaterialKindText(m.kind));
        w.Pair("colour", toml::Array({toml::Float(m.colour.x), toml::Float(m.colour.y), toml::Float(m.colour.z)}));
        w.Pair("opacity", toml::Float(m.opacity));
        w.Pair("light", toml::Float(m.light));
        w.Pair("priority", toml::Float(m.priority));
    }
    return w.text;
}

bool ReadInt(const toml::Value& value, int low, int high, int* out) {
    std::int32_t read = 0;
    if (!toml::Read(value, &read) || read < low || read > high) return false;
    *out = read;
    return true;
}

// map.toml read over `map`: the keys it has replace the map's (a list whole). False, the map as it was, when it does
// not parse or a value does not fit.
bool ReadHeader(std::string_view text, std::string_view path, MapData& map, std::string* error) {
    toml::Document document;
    if (!document.Parse(text, path)) {
        *error = document.Error();
        return false;
    }
    MapData read = map;
    // The header is packed: its fields are read through locals.
    float heightRange = read.header.heightRange;
    int sectorsX = read.header.sectorsX, sectorsY = read.header.sectorsY, pages = read.header.pages;
    int pageSize = read.header.pageSize, tileSize = read.header.tileSize;
    const toml::Value root = document.Root();
    std::vector<std::string> waterRows;
    bool hasWaterRows = false;
    auto bad = [&](std::string_view key) {
        *error = std::format("{}: {} is not one", path, key);
        return false;
    };
    for (int i = 0; i < root.Size(); ++i) {
        const std::string_view key = root.KeyAt(i);
        const toml::Value value = root.ValueAt(i);
        if (key == "name") {
            if (!value.IsString() || value.String().empty()) return bad(key);
            read.name = text::ToAssetText(value.String());
        } else if (key == "height_range") {
            if (!toml::Read(value, &heightRange)) return bad(key);
        } else if (key == "sectors") {
            if (value.Size() != 2 || !ReadInt(value.At(0), 1, kMostSectors, &sectorsX) ||
                !ReadInt(value.At(1), 1, kMostSectors, &sectorsY))
                return bad(key);
        } else if (key == "pages") {
            if (!ReadInt(value, 0, kMostPages, &pages)) return bad(key);
        } else if (key == "page_size") {
            if (!ReadInt(value, 1, 1 << 16, &pageSize)) return bad(key);
        } else if (key == "tile_size") {
            if (!ReadInt(value, 1, 1 << 16, &tileSize)) return bad(key);
        } else if (key == "water_sectors") {
            if (!value.IsArray()) return bad(key);
            hasWaterRows = true;
            for (int row = 0; row < value.Size(); ++row) waterRows.emplace_back(value.At(row).String());
        } else if (key == "classes") {
            if (!value.IsArray()) return bad(key);
            read.classes.assign(static_cast<std::size_t>(value.Size()), 0);
            for (int t = 0; t < value.Size(); ++t)
                if (!ReadClass(value.At(t), &read.classes[static_cast<std::size_t>(t)])) return bad(key);
        } else if (key == "animated") {
            if (!value.IsArray()) return bad(key);
            read.animated.clear();
            for (int a = 0; a < value.Size(); ++a) {
                int texture = 0, frames = 0;
                if (!ReadInt(value.At(a)["texture"], 0, 0xFFFF, &texture) ||
                    !ReadInt(value.At(a)["frames"], 0, 0xFFFF, &frames))
                    return bad(key);
                read.animated.push_back({static_cast<std::uint16_t>(texture), static_cast<std::uint16_t>(frames)});
            }
        } else if (key == "material") {
            if (!value.IsArray() || value.Size() > 0xFFFF) return bad(key);
            read.materials.assign(static_cast<std::size_t>(value.Size()), MapMaterial{});
            for (int m = 0; m < value.Size(); ++m) {
                const toml::Value table = value.At(m);
                MapMaterial& material = read.materials[static_cast<std::size_t>(m)];
                const toml::Value colour = table["colour"];
                if (!ReadMaterialKind(table["kind"], &material.kind)) {
                    *error = std::format("{}: material kind is not one of: dry, water, still, or a number", path);
                    return false;
                }
                if (colour.Size() != 3 ||
                    !toml::Read(colour.At(0), &material.colour.x) || !toml::Read(colour.At(1), &material.colour.y) ||
                    !toml::Read(colour.At(2), &material.colour.z) || !toml::Read(table["opacity"], &material.opacity) ||
                    !toml::Read(table["light"], &material.light) || !toml::Read(table["priority"], &material.priority))
                    return bad(key);
            }
        } else {
            *error = std::format("{}: an unknown key {}", path, key);
            return false;
        }
    }
    if (tileSize > pageSize) return bad("tile_size");
    for (const MapAnimatedTexture& entry : read.animated)
        if (entry.texture >= read.classes.size()) return bad("animated");
    read.header.heightRange = heightRange;
    read.header.sectorsX = sectorsX;
    read.header.sectorsY = sectorsY;
    read.header.pages = pages;
    read.header.pageSize = pageSize;
    read.header.tileSize = tileSize;
    read.Fit();
    if (hasWaterRows) {
        if (std::ssize(waterRows) != sectorsY) return bad("water_sectors");
        for (int sy = 0; sy < sectorsY; ++sy) {
            const std::string& row = waterRows[static_cast<std::size_t>(sy)];
            if (std::ssize(row) != sectorsX) return bad("water_sectors");
            for (int sx = 0; sx < sectorsX; ++sx) {
                if (row[static_cast<std::size_t>(sx)] != kWaterSector &&
                    row[static_cast<std::size_t>(sx)] != kDrySector)
                    return bad("water_sectors");
                read.water[SectorIndex(read, sx, sy)] = row[static_cast<std::size_t>(sx)] == kWaterSector ? 1 : 0;
            }
        }
    }
    map = std::move(read);
    return true;
}

// ---- The seams (seams.toml) --------------------------------------------------------------------------------------

std::string SeamsToml(const MapData& map) {
    toml::Writer w;
    w.Comment(
        "The sectors' own copies of the vertices they share with an earlier sector where they are not the grid's");
    w.Comment("(docs/notes/mods.md, \"Maps as folders\"): what they add to it. ground: heights.png, offsets.png and");
    w.Comment("normals.png; water: water.png and water_normals.png. A normal adds to its x, y and z codes.");
    for (const bool water : {false, true}) {
        std::vector<std::string> items;
        for (const Seam& s : map.seams) {
            if (s.water != water) continue;
            std::vector<std::pair<std::string, std::string>> pairs = {{"sector", IntArray({s.sx, s.sy})},
                                                                      {"vertex", IntArray({s.column, s.row})}};
            if (s.height != 0) pairs.emplace_back("height", toml::Int(s.height));
            if (s.offsetX != 0 || s.offsetY != 0) pairs.emplace_back("offset", IntArray({s.offsetX, s.offsetY}));
            if (s.normal != std::array<int, 3>{})
                pairs.emplace_back("normal", IntArray({s.normal[0], s.normal[1], s.normal[2]}));
            items.push_back(toml::InlineTable(pairs));
        }
        w.Pair(water ? "water" : "ground", toml::Array(items, true));
    }
    return w.text;
}

bool ReadSeams(std::string_view text, std::string_view path, MapData& map, std::string* error) {
    toml::Document document;
    if (!document.Parse(text, path)) {
        *error = document.Error();
        return false;
    }
    std::vector<Seam> seams;
    const toml::Value root = document.Root();
    for (int i = 0; i < root.Size(); ++i) {
        const std::string_view key = root.KeyAt(i);
        const toml::Value list = root.ValueAt(i);
        if ((key != "ground" && key != "water") || !list.IsArray()) {
            *error = std::format("{}: {} is not ground or water", path, key);
            return false;
        }
        for (int k = 0; k < list.Size(); ++k) {
            const toml::Value item = list.At(k);
            Seam s{.water = key == "water"};
            const toml::Value sector = item["sector"], vertex = item["vertex"], offset = item["offset"],
                              normal = item["normal"];
            bool ok = ReadInt(sector.At(0), 0, map.header.sectorsX - 1, &s.sx) &&
                      ReadInt(sector.At(1), 0, map.header.sectorsY - 1, &s.sy) &&
                      ReadInt(vertex.At(0), 0, kSectorCells, &s.column) &&
                      ReadInt(vertex.At(1), 0, kSectorCells, &s.row);
            if (item["height"].Exists()) ok = ok && ReadInt(item["height"], -0xFFFF, 0xFFFF, &s.height);
            if (offset.Exists())
                ok = ok && ReadInt(offset.At(0), -0xFF, 0xFF, &s.offsetX) &&
                     ReadInt(offset.At(1), -0xFF, 0xFF, &s.offsetY);
            if (normal.Exists())
                for (int c = 0; c < 3; ++c)
                    ok = ok && ReadInt(normal.At(c), -0x7FF, 0x7FF, &s.normal[static_cast<std::size_t>(c)]);
            if (!ok) {
                *error = std::format("{}: {} seam {} is not one", path, key, k + 1);
                return false;
            }
            seams.push_back(s);
        }
    }
    map.seams = std::move(seams);
    return true;
}

// ---- The PNG layers ----------------------------------------------------------------------------------------------

// A PNG's samples, `channels` a pixel, at 16 bits (an 8-bit PNG's widened) or 8.
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint16_t> samples;
};

// `png`'s first `channels` channels (a grey PNG's one repeated, a colour PNG's red for one) as 16-bit samples when
// `sixteen` (an 8-bit PNG's widened), else as bytes.
std::optional<Picture> ReadPicture(std::string_view png, int channels, bool sixteen, std::string* error) {
    Picture picture;
    std::vector<std::uint16_t> rgba16;
    std::vector<std::uint8_t> rgba8;
    if (!(sixteen ? png::ReadRgba16(png, &picture.width, &picture.height, &rgba16, error)
                  : png::ReadRgba(png, &picture.width, &picture.height, &rgba8, error))) {
        *error = "not a PNG that reads: " + *error;
        return std::nullopt;
    }
    const std::size_t pixels = static_cast<std::size_t>(picture.width) * picture.height;
    picture.samples.resize(pixels * channels);
    for (std::size_t i = 0; i < pixels; ++i)
        for (int c = 0; c < channels; ++c)
            picture.samples[i * channels + c] = sixteen ? rgba16[i * 4 + c] : rgba8[i * 4 + c];
    return picture;
}

struct LayerKind {
    int channels = 1;
    bool sixteen = false;
    bool vertices = true;   // on the vertex grid, else the tile grid
};

LayerKind KindOf(Layer layer) {
    switch (layer) {
    case Layer::Heights:
    case Layer::Water:
        return {1, true, true};
    case Layer::Normals:
    case Layer::WaterNormals:
        return {3, true, true};
    case Layer::Offsets:
        return {3, false, true};
    case Layer::Tiles:
    case Layer::WaterTiles:
        return {3, false, false};
    default:
        return {1, false, false};   // the water materials
    }
}

// The 16-bit sample of an n-bit code (its bits repeated below it) and back.
std::uint16_t WidenCode(int code, int bits) {
    return static_cast<std::uint16_t>(code << (16 - bits) | code >> (2 * bits - 16));
}
int NarrowCode(std::uint16_t sample, int bits) { return sample >> (16 - bits); }

constexpr int kXYCodeBits = 11;
constexpr int kZCodeBits = 10;

std::vector<std::uint16_t> LayerSamples(const MapData& map, Layer layer) {
    std::vector<std::uint16_t> out;
    const std::vector<Vertex>& vertices =
        layer == Layer::Water || layer == Layer::WaterNormals ? map.waterVertices : map.ground;
    switch (layer) {
    case Layer::Heights:
    case Layer::Water:
        for (const Vertex& v : vertices) out.push_back(v.height);
        break;
    case Layer::Normals:
    case Layer::WaterNormals:
        for (const Vertex& v : vertices) {
            const std::array<int, 3> codes = NormalCodes(v.normal);
            out.insert(out.end(), {WidenCode(codes[0], kXYCodeBits), WidenCode(codes[1], kXYCodeBits),
                                   WidenCode(codes[2], kZCodeBits)});
        }
        break;
    case Layer::Offsets:
        for (const Vertex& v : vertices)
            out.insert(out.end(),
                       {static_cast<std::uint16_t>(kOffsetZero + v.offsetX),
                        static_cast<std::uint16_t>(kOffsetZero + v.offsetY), static_cast<std::uint16_t>(kOffsetZero)});
        break;
    case Layer::Tiles:
    case Layer::WaterTiles:
        for (const std::uint16_t word : layer == Layer::Tiles ? map.tiles : map.waterTiles) {
            const int texture = word & kTextureMask;
            out.insert(out.end(), {static_cast<std::uint16_t>(texture & 0xFF), static_cast<std::uint16_t>(texture >> 8),
                                   static_cast<std::uint16_t>((word >> kTurnShift) * kTurnLevel)});
        }
        break;
    default:
        for (const std::uint16_t material : map.waterMaterials)
            out.push_back(material == MapData::kNone ? kNoMaterialLevel : static_cast<std::uint16_t>(material));
        break;
    }
    return out;
}

void ReadLayer(MapData& map, Layer layer, const Picture& p) {
    std::vector<Vertex>& vertices =
        layer == Layer::Water || layer == Layer::WaterNormals ? map.waterVertices : map.ground;
    const std::vector<std::uint16_t>& s = p.samples;
    switch (layer) {
    case Layer::Heights:
    case Layer::Water:
        for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i].height = s[i];
        break;
    case Layer::Normals:
    case Layer::WaterNormals:
        for (std::size_t i = 0; i < vertices.size(); ++i)
            vertices[i].normal = PackNormal({NarrowCode(s[3 * i], kXYCodeBits), NarrowCode(s[3 * i + 1], kXYCodeBits),
                                             NarrowCode(s[3 * i + 2], kZCodeBits)});
        break;
    case Layer::Offsets:
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            vertices[i].offsetX = static_cast<std::int8_t>(s[3 * i] - kOffsetZero);
            vertices[i].offsetY = static_cast<std::int8_t>(s[3 * i + 1] - kOffsetZero);
        }
        break;
    case Layer::Tiles:
    case Layer::WaterTiles: {
        std::vector<std::uint16_t>& words = layer == Layer::Tiles ? map.tiles : map.waterTiles;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const int turn = std::min((s[3 * i + 2] + kTurnLevel / 2) / kTurnLevel, 3);
            words[i] = static_cast<std::uint16_t>(((s[3 * i] | s[3 * i + 1] << 8) & kTextureMask) | turn << kTurnShift);
        }
        break;
    }
    default:
        for (std::size_t i = 0; i < map.waterMaterials.size(); ++i)
            map.waterMaterials[i] = s[i] == kNoMaterialLevel ? MapData::kNone : s[i];
        break;
    }
}

bool IsPicture(Layer layer) { return layer != Layer::Header && layer != Layer::Seams; }

}   // namespace

std::string_view FileOf(Layer layer) { return kFileNames[layer]; }

void MapData::Fit() {
    const std::size_t vertices = static_cast<std::size_t>(VertexColumns()) * VertexRows();
    const std::size_t tileCount = static_cast<std::size_t>(TileColumns()) * TileRows();
    if (ground.size() != vertices) {
        ground.assign(vertices, Vertex{});
        seams.clear();
    }
    if (waterVertices.size() != vertices) waterVertices.assign(vertices, Vertex{});
    if (tiles.size() != tileCount) tiles.assign(tileCount, 0);
    if (waterTiles.size() != tileCount) waterTiles.assign(tileCount, kNone);
    if (waterMaterials.size() != tileCount) waterMaterials.assign(tileCount, kNone);
    if (water.size() != static_cast<std::size_t>(header.sectorsX) * header.sectorsY)
        water.assign(static_cast<std::size_t>(header.sectorsX) * header.sectorsY, 0);
}

std::string SectorName(std::string_view name, int sx, int sy) { return std::format("{}{:03}{:03}.SEC", name, sx, sy); }

std::optional<MapData> Decode(std::string_view name, std::string_view mp,
                              const std::function<std::optional<std::string>(int sx, int sy)>& sector,
                              std::string* error) {
    MapData map;
    map.name = name;
    if (mp.size() < sizeof(MapHeader)) {
        *error = "the map file is short";
        return std::nullopt;
    }
    map.header = lib::Load<MapHeader>(mp.data());
    const MapHeader& h = map.header;
    const std::uint64_t expected =
        sizeof(MapHeader) + std::uint64_t{h.materials} * sizeof(MapMaterial) +
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(h.textures)) * sizeof(std::int32_t) +
        std::uint64_t{h.animated} * sizeof(MapAnimatedTexture);
    if (h.magic != terrain::kMapMagic || mp.size() != expected || h.textures < 0) {
        *error = "the map file is not one";
        return std::nullopt;
    }
    if (h.sectorsX < 1 || h.sectorsY < 1 || h.sectorsX > kMostSectors || h.sectorsY > kMostSectors) {
        *error = std::format("the map file's {} x {} sectors", h.sectorsX, h.sectorsY);
        return std::nullopt;
    }
    const char* cursor = mp.data() + sizeof(MapHeader);
    for (int i = 0; i < h.materials; ++i, cursor += sizeof(MapMaterial)) {
        MapMaterial material = lib::Load<MapMaterial>(cursor);
        material.warpSpeed = 0.0f;
        std::ranges::fill(material.reserved, std::uint8_t{0});
        map.materials.push_back(material);
    }
    for (int i = 0; i < h.textures; ++i, cursor += sizeof(std::int32_t))
        map.classes.push_back(lib::Load<std::int32_t>(cursor));
    for (std::uint32_t i = 0; i < h.animated; ++i, cursor += sizeof(MapAnimatedTexture))
        map.animated.push_back(lib::Load<MapAnimatedTexture>(cursor));
    map.Fit();

    std::vector<std::string> files(map.water.size());
    for (int sy = 0; sy < h.sectorsY; ++sy)
        for (int sx = 0; sx < h.sectorsX; ++sx) {
            std::optional<std::string> bytes = sector(sx, sy);
            const std::string sectorName = SectorName(name, sx, sy);
            if (!bytes) {
                *error = "no " + sectorName;
                return std::nullopt;
            }
            const std::uint8_t flags = bytes->size() >= kSectorHeaderBytes ? static_cast<std::uint8_t>((*bytes)[4]) : 0;
            const bool water = (flags & kWaterVertices) != 0;
            if (bytes->size() < kSectorHeaderBytes || lib::Load<std::uint32_t>(bytes->data()) != kSectorMagic ||
                bytes->size() != SectorBytes(water) || (flags & kWaterLayers) == (kWaterLayers & ~kWaterVertices)) {
                *error = sectorName + " is not a sector file this reads";
                return std::nullopt;
            }
            map.water[SectorIndex(map, sx, sy)] = water ? 1 : 0;
            files[SectorIndex(map, sx, sy)] = std::move(*bytes);
        }
    auto fileOf = [&](int sx, int sy) {
        return SectorFile{files[SectorIndex(map, sx, sy)], map.water[SectorIndex(map, sx, sy)] != 0};
    };

    // The grids: a vertex from its first copy (the water's from its first copy with water), the tiles as they are.
    for (int y = 0; y < map.VertexRows(); ++y) {
        Copy rows[2], columns[2];
        const int rowCopies = CopiesOf(y, h.sectorsY, rows);
        for (int x = 0; x < map.VertexColumns(); ++x) {
            const int columnCopies = CopiesOf(x, h.sectorsX, columns);
            const std::size_t index = static_cast<std::size_t>(y * map.VertexColumns() + x);
            map.ground[index] =
                fileOf(columns[0].sector, rows[0].sector).VertexAt(0, rows[0].at * kSectorRow + columns[0].at);
            bool found = false;
            for (int r = 0; r < rowCopies && !found; ++r)
                for (int c = 0; c < columnCopies && !found; ++c) {
                    const SectorFile file = fileOf(columns[c].sector, rows[r].sector);
                    if (!file.water) continue;
                    Vertex vertex = file.VertexAt(1, rows[r].at * kSectorRow + columns[c].at);
                    vertex.offsetX = vertex.offsetY = 0;
                    map.waterVertices[index] = vertex;
                    found = true;
                }
        }
    }
    for (int sy = 0; sy < h.sectorsY; ++sy)
        for (int sx = 0; sx < h.sectorsX; ++sx) {
            const SectorFile file = fileOf(sx, sy);
            for (int row = 0; row < kSectorTiles; ++row)
                for (int column = 0; column < kSectorTiles; ++column) {
                    const int i = row * kSectorTiles + column;
                    const std::size_t at = TileIndex(map, sx, sy, column, row);
                    map.tiles[at] = file.WordAt(0, i);
                    if (!file.water) continue;
                    map.waterTiles[at] = file.WordAt(1, i);
                    map.waterMaterials[at] = file.WordAt(2, i);
                }
            // The copies that are not the grid's.
            for (int row = 0; row < kSectorRow; ++row)
                for (int column = 0; column < kSectorRow; ++column) {
                    const std::size_t at = VertexIndex(map, sx, sy, column, row);
                    const int i = row * kSectorRow + column;
                    if (auto seam = SeamOf(file.VertexAt(0, i), map.ground[at], sx, sy, column, row, false))
                        map.seams.push_back(*seam);
                    if (!file.water) continue;
                    if (auto seam = SeamOf(file.VertexAt(1, i), map.waterVertices[at], sx, sy, column, row, true))
                        map.seams.push_back(*seam);
                }
        }
    return map;
}

std::string EncodeMap(const MapData& map) {
    MapHeader h = map.header;
    h.magic = terrain::kMapMagic;
    h.materials = static_cast<std::uint16_t>(map.materials.size());
    h.textures = static_cast<std::int32_t>(map.classes.size());
    h.animated = static_cast<std::uint32_t>(map.animated.size());
    std::string out(sizeof(MapHeader) + map.materials.size() * sizeof(MapMaterial) +
                        map.classes.size() * sizeof(std::int32_t) + map.animated.size() * sizeof(MapAnimatedTexture),
                    '\0');
    char* at = out.data();
    lib::Store(at, h);
    at += sizeof(MapHeader);
    for (MapMaterial material : map.materials) {
        material.warpSpeed = 0.0f;
        std::ranges::fill(material.reserved, std::uint8_t{0});
        lib::Store(at, material);
        at += sizeof(MapMaterial);
    }
    for (const std::int32_t textureClass : map.classes) {
        lib::Store(at, textureClass);
        at += sizeof(std::int32_t);
    }
    for (const MapAnimatedTexture& entry : map.animated) {
        lib::Store(at, entry);
        at += sizeof(MapAnimatedTexture);
    }
    return out;
}

std::string EncodeSector(const MapData& map, int sx, int sy) {
    const bool water = map.water[SectorIndex(map, sx, sy)] != 0;
    std::map<SeamKey, const Seam*> seams;
    for (const Seam& seam : map.seams)
        if (seam.sx == sx && seam.sy == sy) seams[{seam.column, seam.row, seam.water}] = &seam;
    auto seamAt = [&](int column, int row, bool waterLayer) -> const Seam* {
        const auto found = seams.find({column, row, waterLayer});
        return found == seams.end() ? nullptr : found->second;
    };

    std::string out(SectorBytes(water), '\0');
    lib::Store(out.data(), kSectorMagic);
    out[4] = static_cast<char>(water ? kWaterLayers : 0);
    char* at = out.data() + kSectorHeaderBytes;
    for (int layer = 0; layer < (water ? 2 : 1); ++layer)
        for (int row = 0; row < kSectorRow; ++row)
            for (int column = 0; column < kSectorRow; ++column, at += kVertexBytes) {
                const std::size_t index = VertexIndex(map, sx, sy, column, row);
                Vertex vertex = WithSeam(layer == 0 ? map.ground[index] : map.waterVertices[index],
                                         seamAt(column, row, layer == 1));
                if (layer == 1) vertex.offsetX = vertex.offsetY = 0;
                lib::Store(at, vertex.offsetX);
                lib::Store(at + 1, vertex.offsetY);
                lib::Store(at + 2, vertex.height);
                lib::Store(at + 4, vertex.normal);
            }
    for (int tileLayer = 0; tileLayer < (water ? 3 : 1); ++tileLayer) {
        const std::vector<std::uint16_t>& words = tileLayer == 0   ? map.tiles
                                                  : tileLayer == 1 ? map.waterTiles
                                                                   : map.waterMaterials;
        for (int row = 0; row < kSectorTiles; ++row)
            for (int column = 0; column < kSectorTiles; ++column, at += 2)
                lib::Store(at, words[TileIndex(map, sx, sy, column, row)]);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> WriteFolder(const MapData& map) {
    std::vector<std::pair<std::string, std::string>> files;
    files.emplace_back(FileOf(Layer::Header), HeaderToml(map));
    for (int i = 0; i < static_cast<int>(Layer::Count); ++i) {
        const Layer layer = static_cast<Layer>(i);
        if (!IsPicture(layer)) continue;
        const LayerKind kind = KindOf(layer);
        const int width = kind.vertices ? map.VertexColumns() : map.TileColumns();
        const int height = kind.vertices ? map.VertexRows() : map.TileRows();
        files.emplace_back(FileOf(layer),
                           png::Write(width, height, kind.channels, kind.sixteen ? 16 : 8, LayerSamples(map, layer)));
    }
    if (!map.seams.empty()) files.emplace_back(FileOf(Layer::Seams), SeamsToml(map));
    return files;
}

void WorkOutNormals(MapData& map, bool water) {
    std::vector<Vertex>& layer = water ? map.waterVertices : map.ground;
    const int columns = map.VertexColumns(), rows = map.VertexRows();
    const float scale = map.header.heightRange / kHeightUnits;
    auto position = [&](int x, int y) {
        const Vertex& v = layer[static_cast<std::size_t>(y * columns + x)];
        const float dx = water ? 0.0f : v.offsetX * kOffsetUnit, dy = water ? 0.0f : v.offsetY * kOffsetUnit;
        return Vector3{static_cast<float>(x) + dx, static_cast<float>(y) + dy, v.height * scale};
    };
    constexpr int kSteps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};   // each with the next a face, counter-clockwise
    std::vector<std::uint32_t> normals(layer.size());
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            const Vector3 centre = position(x, y);
            Vector3 sum{0.0f, 0.0f, 0.0f};
            for (int face = 0; face < 4; ++face) {
                const int* a = kSteps[face];
                const int* b = kSteps[(face + 1) % 4];
                if (x + a[0] < 0 || x + a[0] >= columns || y + a[1] < 0 || y + a[1] >= rows || x + b[0] < 0 ||
                    x + b[0] >= columns || y + b[1] < 0 || y + b[1] >= rows)
                    continue;
                sum += NormalisedUnlessZero(
                    Cross(position(x + a[0], y + a[1]) - centre, position(x + b[0], y + b[1]) - centre));
            }
            const Vector3 n = NormalisedUnlessZero(sum);
            const int codeX =
                std::clamp(lib::RoundEven(n.x * kNormalUnits) + kNormalBias, 0, static_cast<int>(kNormalXYMask));
            const int codeY =
                std::clamp(lib::RoundEven(n.y * kNormalUnits) + kNormalBias, 0, static_cast<int>(kNormalXYMask));
            const int codeZ = std::clamp(lib::RoundEven(n.z * kNormalUnits), 0, static_cast<int>(kNormalZMask));
            normals[static_cast<std::size_t>(y * columns + x)] =
                LengthSquared(n) == 0.0f ? Vertex::kUp : PackNormal({codeX, codeY, codeZ});
        }
    for (std::size_t i = 0; i < layer.size(); ++i) layer[i].normal = normals[i];
}

std::optional<MapData> Assemble(const std::vector<Place>& places, std::string_view folderName,
                                std::vector<std::string>* warnings) {
    constexpr std::size_t kNowhere = SIZE_MAX;
    lib::EnumArray<std::size_t, Layer, static_cast<std::size_t>(Layer::Count)> at;
    bool any = false;
    for (int i = 0; i < static_cast<int>(Layer::Count); ++i) {
        at[i] = kNowhere;
        for (std::size_t p = 0; p < places.size() && at[i] == kNowhere; ++p)
            if (places[p].has(kFileNames[i])) at[i] = p;
        any = any || at[i] != kNowhere;
    }
    if (!any) return std::nullopt;
    std::size_t original = kNowhere;
    for (std::size_t p = 0; p < places.size() && original == kNowhere; ++p)
        if (!places[p].mapFile.empty()) original = p;
    // A file of the folder counts at a place no lower than the original's.
    auto used = [&](Layer layer) { return at[layer] != kNowhere && (original == kNowhere || at[layer] <= original); };
    bool anyUsed = false;
    for (int i = 0; i < static_cast<int>(Layer::Count); ++i) anyUsed = anyUsed || used(static_cast<Layer>(i));
    if (!anyUsed) return std::nullopt;

    auto read = [&](Layer layer, std::string* bytes) { return places[at[layer]].read(kFileNames[layer], bytes); };
    MapData map;
    bool hasOriginal = false;
    if (original != kNowhere) {
        const std::string& mapFile = places[original].mapFile;
        const std::string name = mapFile.substr(0, mapFile.find('.'));
        std::string mp, error;
        places[original].read(mapFile, &mp);
        std::optional<MapData> decoded = Decode(
            name, mp,
            [&](int sx, int sy) -> std::optional<std::string> {
                const std::string sectorName = SectorName(name, sx, sy);
                std::string bytes;
                for (const Place& place : places)
                    if (place.has(sectorName) && place.read(sectorName, &bytes)) return bytes;
                return std::nullopt;
            },
            &error);
        if (decoded) {
            map = std::move(*decoded);
            hasOriginal = true;
        } else {
            warnings->push_back(mapFile + ": " + error + "; the folder's files alone");
        }
    }
    if (!hasOriginal) map.name = folderName;

    std::string bytes, error;
    if (used(Layer::Header) && read(Layer::Header, &bytes)) {
        if (!ReadHeader(bytes, kFileNames[Layer::Header], map, &error)) warnings->push_back(error + "; not read");
    } else if (!hasOriginal) {
        warnings->push_back(std::string("no ") + std::string(kFileNames[Layer::Header]) + " and no map file: no map");
        return std::nullopt;
    }
    if (map.header.sectorsX < 1 || map.header.sectorsY < 1 || map.header.tileSize < 1) {
        warnings->push_back(std::string(kFileNames[Layer::Header]) + " gives no size: no map");
        return std::nullopt;
    }
    map.header.magic = terrain::kMapMagic;
    map.Fit();

    for (int i = 0; i < static_cast<int>(Layer::Count); ++i) {
        const Layer layer = static_cast<Layer>(i);
        if (layer == Layer::Header || !used(layer) || !read(layer, &bytes)) continue;
        const std::string file(kFileNames[layer]);
        if (layer == Layer::Seams) {
            if (!ReadSeams(bytes, file, map, &error)) warnings->push_back(error + "; not read");
            continue;
        }
        const LayerKind kind = KindOf(layer);
        const std::optional<Picture> picture = ReadPicture(bytes, kind.channels, kind.sixteen, &error);
        if (!picture) {
            warnings->push_back(file + ": " + error + "; not read");
            continue;
        }
        const int width = kind.vertices ? map.VertexColumns() : map.TileColumns();
        const int height = kind.vertices ? map.VertexRows() : map.TileRows();
        if (picture->width != width || picture->height != height) {
            warnings->push_back(std::format("{}: {} x {}, the map's {} grid is {} x {}; not read", file, picture->width,
                                            picture->height, kind.vertices ? "vertex" : "tile", width, height));
            continue;
        }
        ReadLayer(map, layer, *picture);
    }
    // Heights from above their normals bring their own normals: a layer's rank is its place's, a place's file of the
    // folder before its original.
    auto rankOf = [&](Layer layer) { return used(layer) ? 2 * at[layer] : hasOriginal ? 2 * original + 1 : kNowhere; };
    std::size_t normalsRank[2] = {rankOf(Layer::Normals), rankOf(Layer::WaterNormals)};
    for (const bool water : {false, true}) {
        const std::size_t heightsRank = rankOf(water ? Layer::Water : Layer::Heights);
        if (normalsRank[water] <= heightsRank) continue;
        WorkOutNormals(map, water);
        normalsRank[water] = heightsRank;
    }
    // Seams from below a layer are differences from the values it replaced: left out of it (painted heights meet,
    // worked-out normals are the same in every sector).
    const std::size_t seamsRank = rankOf(Layer::Seams);
    for (Seam& seam : map.seams) {
        if (seamsRank > rankOf(seam.water ? Layer::Water : Layer::Heights)) seam.height = 0;
        if (seamsRank > rankOf(Layer::Offsets)) seam.offsetX = seam.offsetY = 0;
        if (seamsRank > normalsRank[seam.water]) seam.normal = {};
    }
    std::erase_if(map.seams, [](const Seam& seam) {
        return seam.height == 0 && seam.offsetX == 0 && seam.offsetY == 0 && seam.normal == std::array<int, 3>{};
    });
    // A water material the header does not have is none (the landscape reads the material of each water vertex).
    int strays = 0;
    for (std::uint16_t& material : map.waterMaterials)
        if (material != MapData::kNone && material >= map.materials.size()) {
            material = MapData::kNone;
            ++strays;
        }
    if (strays > 0)
        warnings->push_back(
            std::format("{} water tiles of a material past the {}: none", strays, map.materials.size()));
    return map;
}

}   // namespace map_folder
