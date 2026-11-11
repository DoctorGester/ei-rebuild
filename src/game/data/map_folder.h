#pragma once

// Maps as folders (docs/notes/mods.md, "Maps as folders"): a map archive's (.mpr) map file and sectors as a TOML
// header and PNG layers in the archive's folder (Maps\zone1\ for Maps\zone1.mpr), and the files the game reads made
// from them again. Decode takes a map file and its sectors apart into whole-map layers, EncodeMap / EncodeSector make
// the files again, WriteFolder writes the folder's files and Assemble reads them over the original, place by place.
// No game state: the archives (data/resource.cpp), ei-convert and the unit check use it as it is.

#include "game/data/map_file.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace map_folder {

// The folder's files, each a layer of the map.
enum class Layer {
    Header,
    Heights,
    Offsets,
    Normals,
    Water,
    WaterNormals,
    Tiles,
    WaterTiles,
    WaterMaterials,
    Seams,
    Count
};
// A layer's file name ("map.toml", "heights.png", ...).
std::string_view FileOf(Layer layer);

// A vertex as a sector file holds it: the sideways displacement (1/252 cell a step), the height (in 1/65534 of the
// map's height range) and the packed normal (z in the top 10 bits, then x and y in 11 bits each; a code is the
// component in thousandths, x's and y's plus 1000).
struct Vertex {
    std::int8_t offsetX = 0;
    std::int8_t offsetY = 0;
    std::uint16_t height = 0;
    std::uint32_t normal = kUp;

    static constexpr std::uint32_t kUp = 1000u << 22 | 1000u << 11 | 1000u;   // (0, 0, 1)
    bool operator==(const Vertex&) const = default;
};

// A sector's own copy of a vertex it shares with an earlier sector (its column or row 0) where it is not the grid's:
// what it adds to the grid's values. The packed normal's codes add one by one.
struct Seam {
    int sx = 0, sy = 0;        // the sector
    int column = 0, row = 0;   // the vertex in it, 0 .. 32
    bool water = false;        // of the water layer (no offsets)
    int height = 0;
    int offsetX = 0, offsetY = 0;
    std::array<int, 3> normal = {};   // x, y, z codes
    bool operator==(const Seam&) const = default;
};

// A map: what its map file and sectors hold that the loaders read, as whole-map layers. Vertices on a grid of
// (32 sectorsX + 1) x (32 sectorsY + 1) from vertex (0, 0), row by row; a vertex two sectors share is the earlier
// sector's (the one the height queries read), the later one's copy where it differs is a seam. The water grid holds the
// first copy among the sectors with water. Tiles on a grid of 16 sectorsX x 16 sectorsY. A sector without water has no
// water layer: its water tiles and materials are kNone.
struct MapData {
    static constexpr std::uint16_t kNone = 0xFFFF;   // no tile, no water material

    std::string name;            // the files' name: <name>.MP, <name>XXXYYY.SEC (the terrain pages are <name>NNN)
    terrain::MapHeader header;   // its counts are the lists' below
    std::vector<terrain::MapMaterial> materials;
    std::vector<std::int32_t> classes;   // per tile texture
    std::vector<terrain::MapAnimatedTexture> animated;
    std::vector<std::uint8_t> water;   // per sector (sy * sectorsX + sx): 1 with a water layer
    std::vector<Vertex> ground;
    std::vector<Vertex> waterVertices;   // no displacements (the landscape places them on the grid)
    std::vector<std::uint16_t> tiles;    // tile words: the texture in the low 14 bits, the quarter turns in the top two
    std::vector<std::uint16_t> waterTiles;
    std::vector<std::uint16_t> waterMaterials;
    std::vector<Seam> seams;

    int VertexColumns() const { return header.sectorsX * 32 + 1; }
    int VertexRows() const { return header.sectorsY * 32 + 1; }
    int TileColumns() const { return header.sectorsX * 16; }
    int TileRows() const { return header.sectorsY * 16; }
    // Every layer the header's size (a layer of another size starts again from nothing: no water, flat, no turn).
    void Fit();
};

// The sector file's name of sector (sx, sy): <name>XXXYYY.SEC.
std::string SectorName(std::string_view name, int sx, int sy);

// The map of map file `mp` named `name` and its sectors (`sector(sx, sy)`: the file's bytes, nullopt when missing);
// nullopt with `*error` when the map file is not one, or a sector is missing or not one (or has water tiles without
// water, which the terrain cannot read).
std::optional<MapData> Decode(std::string_view name, std::string_view mp,
                              const std::function<std::optional<std::string>(int sx, int sy)>& sector,
                              std::string* error);
// The map file, and sector (sx, sy)'s file, of `map` (the materials' unread bytes zero, a sector's flag byte 3 with
// water and 0 without, the water vertices' unread offsets zero).
std::string EncodeMap(const MapData& map);
std::string EncodeSector(const MapData& map, int sx, int sy);

// The folder's files of `map`, every layer (seams.toml only when there are seams): (file name, bytes).
std::vector<std::pair<std::string, std::string>> WriteFolder(const MapData& map);

// A place where a map's files are looked for (a folder of a layer of the path map, an archive's entries), as the
// archive's GetFile looks: whether it has a file (`has`), its bytes (`read`), and the name of its first map file (.mp)
// as the archive lists it (`mapFile`, empty when it has none).
struct Place {
    std::function<bool(std::string_view name)> has;
    std::function<bool(std::string_view name, std::string* bytes)> read;
    std::string mapFile;
};

// The map of `places` (top first; docs/notes/mods.md, "Maps as folders"): the original (the first place with a map
// file, its sectors each from the first place that has it) with each of the folder's files read over it that a place
// no lower than the original's has (the first such); `folderName` names a map without an original or a name of its
// own. Heights from a place above the normals' come with normals worked out from them. Nullopt when no place above the
// original has a file of the folder (the original is then read as it is), or with neither an original nor a header.
// `warnings` gets a line for each file left out (and why).
std::optional<MapData> Assemble(const std::vector<Place>& places, std::string_view folderName,
                                std::vector<std::string>* warnings);

// The pieces, for the checks.
// The normals of a layer worked out from its heights (and the ground's displacements): each vertex's the mean of the
// four faces around it.
void WorkOutNormals(MapData& map, bool water);

}   // namespace map_folder
