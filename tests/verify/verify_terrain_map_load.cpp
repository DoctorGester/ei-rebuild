// Check for the map loader Terrain::LoadMap (006C68B0, A-H26; Rebirth redirects one of its calls,
// so it is hooked in the vanilla launch only).
// Side by side on the real landscape. The function starts by freeing the map (ClearMap: the
// sectors, the page textures and materials, the archive, the sector grid, the uv tables), so the
// second run replaces what the first made; what the first left is recorded before: the result, the
// three texts, the sizes, the materials' written fields (the bytes the loader leaves alone are
// not), the textures' classes and frame counts, the eight uv tables, the page counts, the ambient
// sound classes' one-shot counts, the flags, and that the sector grid and the water levels are
// sized and zero. Floats are compared by their bits. Between the runs the two things the loader
// only adds to are put back: the extra page count (SetupPages counts it up, only Reset clears it)
// and the flags. A map the original does not load (no archive, a texture missing, or FatalError's
// "Unable to open file" / "Wrong file format") is not loaded a second time.
// The cost: the page textures are released and loaded again, and the one-shot probing takes a
// second reference on each wave file it finds (the original never gives those back either).
// Scenario: every one that enters a zone (the client world's map setup).
#include "verify.h"

#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kSoundClasses = 16;
constexpr int kUvFloats = 18;

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct MapShot {
    int result = 0;
    std::string name, path, set;
    std::vector<std::uint32_t> sizes;       // the landscape's own numbers
    std::vector<std::uint32_t> materials;   // the written fields of each
    std::vector<int> classes, frames, variants;
    std::vector<std::uint32_t> uv;
    bool gridEmpty = false;
    bool levelsZero = false;
};

void AddVector(std::vector<std::uint32_t>& out, const Vector3& v) {
    out.push_back(Bits(v.x));
    out.push_back(Bits(v.y));
    out.push_back(Bits(v.z));
}

MapShot Take(const Terrain* t, int result) {
    MapShot shot;
    shot.result = result;
    shot.sizes = {static_cast<std::uint32_t>(t->mapLoaded), static_cast<std::uint32_t>(t->sectorsX),
                  static_cast<std::uint32_t>(t->sectorsY)};
    if (result != 1) return shot;
    shot.name = t->textF8;
    shot.path = t->textFC;
    shot.set = t->textAE0;
    for (std::uint32_t value :
         {Bits(t->width), Bits(t->height), Bits(t->heightRange), static_cast<std::uint32_t>(t->pageSize),
          static_cast<std::uint32_t>(t->pages), static_cast<std::uint32_t>(t->tileSize), static_cast<std::uint32_t>(t->tilesPerRow),
          static_cast<std::uint32_t>(t->perPage), t->landscapeFlags, static_cast<std::uint32_t>(t->extraPages),
          static_cast<std::uint32_t>(t->pageTextures.size), static_cast<std::uint32_t>(t->pageMaterials.size),
          static_cast<std::uint32_t>(t->materials.size), static_cast<std::uint32_t>(t->waterLevels.size),
          static_cast<std::uint32_t>(t->archive.entries_ != nullptr)})
        shot.sizes.push_back(value);

    for (int i = 0; i < t->materials.size; ++i) {
        const terrain::TerrainMaterial& m = t->materials[i];
        shot.materials.push_back(static_cast<std::uint32_t>(m.kind));
        AddVector(shot.materials, m.waveColour);
        shot.materials.push_back(Bits(m.opacity));
        shot.materials.push_back(Bits(m.absorption));
        shot.materials.push_back(m.opacityByte);
        AddVector(shot.materials, m.colour);
        shot.materials.push_back(Bits(m.priority));
        shot.materials.push_back(Bits(m.warpSpeed));
        shot.materials.push_back(Bits(m.level));
    }
    shot.classes.assign(t->textureClass.data, t->textureClass.data + t->textureClass.size);
    shot.frames.assign(t->textureFrames.data, t->textureFrames.data + t->textureFrames.size);
    for (int i = 0; i < kSoundClasses; ++i) shot.variants.push_back(terrain::AmbientVariants(i));

    const int perPage = t->perPage > 0 ? t->perPage : 0;
    const int perRow = t->tilesPerRow > 0 ? t->tilesPerRow : 0;
    for (const float* table : t->uvTables)
        for (int i = 0; table && i < perPage * kUvFloats; ++i) shot.uv.push_back(Bits(table[i]));
    // The last pages' tables are filled for one row of tiles; the rest is whatever the heap held.
    for (const float* table : t->uvTablesLast)
        for (int i = 0; table && i < perRow * kUvFloats; ++i) shot.uv.push_back(Bits(table[i]));

    shot.gridEmpty = t->sectorGrid != nullptr;
    for (int x = 0; shot.gridEmpty && x < t->sectorsX; ++x) {
        shot.gridEmpty = t->sectorGrid[x] != nullptr;
        for (int y = 0; shot.gridEmpty && y < t->sectorsY; ++y) shot.gridEmpty = t->sectorGrid[x][y] == nullptr;
    }
    shot.levelsZero = t->waterLevels.size == t->sectorsX * t->sectorsY;
    for (int i = 0; shot.levelsZero && i < t->waterLevels.size; ++i) {
        const mfc::CArray<float>& levels = t->waterLevels[i];
        shot.levelsZero = levels.size == t->materials.size;
        for (int k = 0; shot.levelsZero && k < levels.size; ++k) shot.levelsZero = Bits(levels[k]) == 0;
    }
    return shot;
}

// The first part that differs, or null.
const char* FirstDifference(const MapShot& a, const MapShot& b) {
    if (a.result != b.result) return "the result";
    if (a.name != b.name || a.path != b.path) return "the map's name or path";
    if (a.set != b.set) return "the ambient sound set";
    if (a.sizes != b.sizes) return "the sizes, flags or counts";
    if (a.materials != b.materials) return "a material";
    if (a.classes != b.classes) return "a texture's class";
    if (a.frames != b.frames) return "a texture's frame count";
    if (a.variants != b.variants) return "a sound class's one-shot count";
    if (a.uv != b.uv) return "a uv table";
    if (a.gridEmpty != b.gridEmpty) return "the sector grid";
    if (a.levelsZero != b.levelsZero) return "the water levels";
    return nullptr;
}

// 006C68B0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
int(__fastcall* s_loadMap)(Terrain*, void*, const char*, const char*, char);

int __fastcall LoadMapVerify(Terrain* self, void*, const char* path, const char* zone, char dungeon) {
    const int extraPages = self->extraPages;
    const std::uint32_t flags = self->landscapeFlags;
    const int expected = s_loadMap(self, nullptr, path, zone, dungeon);
    if (expected != 1) return expected;
    const MapShot first = Take(self, expected);

    self->extraPages = extraPages;
    self->landscapeFlags = flags;
    const int actual = self->Terrain::LoadMap(path, zone, dungeon);
    const MapShot second = Take(self, actual);
    const char* difference = FirstDifference(first, second);
    const bool complete = first.gridEmpty && first.levelsZero;
    ei::verify::Check("Terrain::LoadMap", true, difference == nullptr && complete,
                      "\"%s\" zone \"%s\" dungeon %d: result %d / %d, %d x %d sectors, %d materials, %d textures; differs in %s; "
                      "the original's grid empty %d, levels zero %d",
                      path, dungeon ? "(not read)" : zone, dungeon, expected, actual, self->sectorsX, self->sectorsY, self->materials.size,
                      self->textureFrames.size, difference ? difference : "nothing", first.gridEmpty, first.levelsZero);
    return actual;
}
EI_VERIFY(0x006C68B0, LoadMapVerify, 5, s_loadMap);

} // namespace
