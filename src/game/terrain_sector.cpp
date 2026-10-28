// Terrain sectors: loading, building, water levels and the small
// lookups around them. See terrain_sector.h for the layout.
// Floating point: values kept on the FPU stack are `double` here; every store to a float is a
// static_cast<float>. Constants multiplied at run time are read from the exe (rule 9: Rebirth may patch
// data).
#include "terrain.h"
#include "terrain_sector.h"
#include "model.h"

#include "ei/x87.h"
#include "random.h"
#include "../lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "render/render.h"
#include "lib/nan_audit.h"
#include "lib/static_object.h"

namespace terrain {
namespace {

// The sector files' extension.
constexpr const char* kSectorExtension = /*00744164*/ ".SEC";

// The constants of this code (all floats in .rdata).
constexpr double kSectorCells = 32.0;
constexpr double kOffsetScale = 0.007936508394777775;  // 1/126: vertex displacement per unit
constexpr double kHeightUnit = 0.000015259254723787308;   // 1/65534 (37800100)
constexpr double kNormalScale = 0.0010000000474974513;  // 0.001
constexpr double kNormalBias = 1000.0;

constexpr std::uint32_t kSectorMagic = 0xCF4BF774;
constexpr int kVertices = 33 * 33;      // per layer

// Texture page of a tile texture: the first `pages` pages hold `perPage` textures each, the
// last ones 8 each.
int PageOf(int texture, int perPage, int pages) {
    int page = texture / perPage;
    if (page >= pages) page = (texture - perPage * pages) / 8 + pages;
    return page;
}

int IndexInPage(int texture, int perPage, int pages) {
    if (texture / perPage < pages) return texture % perPage;
    return (texture - perPage * pages) % 8;
}

// The animation speed of a texture class (the exe's table of 16).
constexpr int kAnimationSpeeds[16] = /*00796960*/ {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 5, 9};

int ClassOf(const Terrain* owner, int texture) {
    int cls = owner->textureClass[texture];
    if (cls >= owner->perPage * owner->pages) cls = 0xF;
    return cls;
}

bool IsAnimated(const Terrain* owner, int texture) {
    return texture < owner->perPage * owner->pages && owner->textureFrames[texture] != 0;
}

// Vertex records: water material (water layer), normals, height range. Returns the tile words.
const std::uint8_t* BuildRecords(SectorData& s) {
    const std::uint8_t* tiles = s.data;
    const std::uint8_t* src = s.data;
    SectorVertex* rec = s.records.data();
    const SectorPosition* vin = s.vertices.data();
    std::memcpy(&s.minZ, "\xca\xf2\x49\x71", 4);    // 1e30
    s.maxZ = 0.0f;
    for (int layer = 0; layer < (s.hasWater ? 2 : 1); ++layer) {
        if (layer == 1) tiles += 0x2208;
        for (int vi = 0; vi < kVertices; ++vi, ++rec, ++vin, src += 8) {
            // Branch-free selects on the comparison: the minimum keeps its value when it is
            // below z, the maximum when z is below it (and both when z is NaN).
            float z = vin->z;
            if (z <= s.minZ) s.minZ = z;
            if (z >= s.maxZ) s.maxZ = z;

            rec->materialByte = 0xFF;
            rec->materialColour = {0.0f, 0.0f, 0.0f};
            rec->waterMaterial = 0xFFFF;
            if (layer == 1) {
                // The first water material among the vertex's tiles.
                const VertexTiles& around = s.owner->vertexTiles[vi];
                int m = s.waterTiles[around.tiles[0]];
                for (int i = 1; m == -1 && i < around.count; ++i) m = s.waterTiles[around.tiles[i]];
                rec->waterMaterial = static_cast<std::uint16_t>(m);
                if (m != -1) {
                    const TerrainMaterial& mat = s.owner->materials[m];
                    rec->materialByte = mat.opacityByte;
                    rec->materialColour = mat.colour;
                }
                std::memcpy(&rec->position, vin, 12);
                (rec - kVertices)->depth = static_cast<float>(static_cast<double>(vin->z) - (vin - kVertices)->z);
            }
            std::uint32_t n;
            std::memcpy(&n, src + 4, 4);
            rec->normal.z = static_cast<float>(x87::FromUnsigned(n >> 22) * kNormalScale);
            rec->normal.x = static_cast<float>((static_cast<double>(static_cast<int>((n >> 11) & 0x7FF)) - kNormalBias) * kNormalScale);
            rec->normal.y = static_cast<float>((static_cast<double>(static_cast<int>(n & 0x7FF)) - kNormalBias) * kNormalScale);
        }
    }
    return tiles + 0x2208;
}

// Counts the tiles per quarter and texture page (counts[quarter * 256 + page]), stores the tile
// textures and rotations and the texture class counts. Returns the number of animated tiles.
int CountTiles(SectorData& s, const std::uint8_t* tiles, int* counts) {
    const Terrain* owner = s.owner;
    int animated = 0;
    s.classCounts.resize(16);
    std::memset(s.classCounts.data(), 0, 16 * 4);
    s.tileTextures.resize(s.hasWaterTiles ? 0x200 : 0x100);

    const std::uint8_t* src = tiles;
    int* out = s.tileTextures.data();
    std::uint8_t* rotation = s.tileRotation;
    for (int y = 0; y < 32; y += 2) {
        for (int x = 0; x < 32; x += 2, src += 2, ++out) {
            std::uint16_t word;
            std::memcpy(&word, src, 2);
            *rotation++ = static_cast<std::uint8_t>(word >> 14);
            int texture = word & 0x3FFF;
            int page = PageOf(texture, s.perPage, s.pages);
            int quarter = x / 16 + (y / 16) * 2;
            *out = texture;
            if (IsAnimated(owner, texture)) ++animated;
            ++counts[quarter * 256 + page];
            ++s.classCounts[ClassOf(owner, texture)];
        }
    }
    if (s.hasWaterTiles) {
        const int* water = s.waterTiles.data();
        for (int y = 0; y < 32; y += 2) {
            for (int x = 0; x < 32; x += 2, src += 2, ++out) {
                if (*water++ == -1) {
                    *out = -1;
                    continue;
                }
                std::uint16_t word;
                std::memcpy(&word, src, 2);
                int texture = word & 0x3FFF;
                int page = s.pages + PageOf(texture, s.perPage, s.pages) + 2;
                int quarter = x / 16 + (y / 16) * 2;
                *out = texture;
                if (IsAnimated(owner, texture)) ++animated;
                ++counts[quarter * 256 + page];
                ++s.classCounts[ClassOf(owner, texture)];
            }
        }
    }
    return animated;
}

// Fills the tile lists: 9 entries per tile (3x3 vertices with texture coordinates), its edge
// flags, and the animated tiles (random start frame).
void FillTiles(SectorData& s, const std::uint8_t* tiles, int* counts) {
    Terrain* owner = s.owner;
    const std::uint8_t* src = tiles;
    const int* water = s.waterTiles.data();
    int animatedIndex = 0;
    int base = 0, pageAdd = 0;
    for (int layer = 0; layer < (s.hasWaterTiles ? 2 : 1); ++layer) {
        if (layer == 1) {
            pageAdd = s.pages + 2;
            base = kVertices;
            water = s.waterTiles.data();
        }
        for (int row = 0; row < 16; ++row) {
            for (int col = 0; col < 16; ++col) {
                if (layer == 1 && *water++ == -1) {
                    src += 2;
                    continue;
                }
                std::uint16_t word;
                std::memcpy(&word, src, 2);
                if (word == 0xFFFF) {
                    src += 2;
                    continue;
                }
                int vi = base + 2 * (col + 33 * row);
                int texture = word & 0x3FFF;
                int page = PageOf(texture, s.perPage, s.pages) + pageAdd;
                int quarter = (2 * row) / 16 * 2 + (2 * col) / 16;
                int rotation = word >> 14;
                int index = IndexInPage(texture, s.perPage, s.pages);
                const float* uv = owner->TileUv(rotation, index, page);
                src += 2;

                int& count = counts[quarter * 256 + page];
                TileVertex* entry = s.tiles[quarter][page].data() + count * 9;
                SectorVertex* rec = s.records.data() + vi;
                if (texture < owner->perPage * owner->pages) {
                    int frames = owner->textureFrames[texture];
                    if (frames != 0) {
                        AnimatedTile& a = s.animated[animatedIndex++];
                        a.frameBase = index;
                        a.vertices = entry;
                        a.rotation = rotation;
                        std::uint32_t r = g_random.Next();
                        a.frames = frames;
                        a.frame = static_cast<int>(r % static_cast<std::uint32_t>(frames));
                        a.speed = kAnimationSpeeds[ClassOf(owner, texture)];
                        uv = owner->TileUv(rotation, a.frame + index, 0);
                    }
                }
                for (int r3 = 0; r3 < 3; ++r3, rec += 33) {
                    for (int c3 = 0; c3 < 3; ++c3, ++entry) {
                        std::memcpy(&entry->u, &uv[r3 * 3 + c3], 4);
                        entry->vertex = rec + c3;
                        entry->v = static_cast<float>(1.0 - uv[r3 * 3 + c3 + 9]);
                    }
                }
                std::uint8_t& flags = s.tileFlags[quarter][page][count];
                flags = row == 0 ? 1 : (row == 15 ? 2 : 0);
                if (col == 0) flags |= 4;
                else if (col == 15) flags |= 8;
                ++count;
            }
        }
    }
}

// Bounding boxes of the sector and of its four quarters.
void ComputeBounds(SectorData& s) {
    double size = kSectorCells;
    float x0 = static_cast<float>(s.sx * size);
    float y0 = static_cast<float>(s.sy * size);
    double half = size * 0.5;
    s.maxZ = static_cast<float>(1.0 + s.maxZ);
    double low = static_cast<double>(s.minZ) - 1.0;
    s.minZ = static_cast<float>(low);
    double cz = (static_cast<double>(s.maxZ) - low) * 0.5 + low;
    float centreZ = static_cast<float>(cz);
    s.box.center.x = static_cast<float>(half + x0);
    s.box.center.y = static_cast<float>(half + y0);
    s.box.center.z = centreZ;
    s.box.boxMin.x = static_cast<float>(-half);
    s.box.boxMin.y = static_cast<float>(-half);
    s.box.boxMin.z = static_cast<float>(s.minZ - cz);
    s.box.boxMax.x = static_cast<float>(half);
    s.box.boxMax.y = static_cast<float>(half);
    s.box.boxMax.z = static_cast<float>(s.maxZ - cz);
    s.box.flags |= 2;
    double q = half * 0.5;
    double r = half * (-0.5);
    SectorBox* box = s.quarters;
    for (int i = 0; i < 2; ++i) {
        float by = static_cast<float>((i + 0.5) * half + y0);
        for (int j = 0; j < 2; ++j, ++box) {
            box->center.x = static_cast<float>((j + 0.5) * half + x0);
            box->center.y = by;
            box->center.z = centreZ;
            box->boxMin.x = static_cast<float>(r);
            box->boxMin.y = static_cast<float>(r);
            box->boxMin.z = static_cast<float>(s.minZ - cz);
            box->boxMax.x = static_cast<float>(q);
            box->boxMax.y = static_cast<float>(q);
            box->boxMax.z = static_cast<float>(s.maxZ - cz);
            box->flags |= 2;
        }
    }
}

double RawHeight(Terrain* terrain, float x, float y, int layer) {
    return terrain->HeightAt(x, y, layer);   // used unrounded
}

constinit std::vector<Vector4> g_projectionInput = /*007C88F8*/ {};
constinit std::vector<Vector4> g_projectionOutput = /*007C88E8*/ {};

// The exit functions their static initialisers register: each frees its buffer, once (a guard bit each in
// g_freed; bit 4 is the outcodes' destructor's).
std::uint8_t g_freed = /*007C88C9*/ {};
// The projected vertices' outcodes (a byte array): built empty by its static initialiser, destroyed once at
// exit.
constinit lib::StaticObject<std::vector<std::uint8_t>> g_projectionOutcodes = /*007C88D0*/ {};
void DestroyProjectionOutcodes() {
    if (g_freed & 4) return;
    g_freed = static_cast<std::uint8_t>(g_freed | 4);
    g_projectionOutcodes.Destroy();
}
constexpr int kProjectionVertices = 0x330;

constexpr int kQuarterCells = 16;
constexpr int kRow = 33;

} // namespace

// Loading

int SectorData::Load(CMappedResource* archive, int sectorX, int sectorY, int pageCount, int textures, const char* prefix,
                     Terrain* terrain) {
    Clear();
    owner = terrain;
    sx = sectorX;
    pages = pageCount;
    sy = sectorY;
    perPage = textures;

    char name[0x100];
    std::snprintf(name, sizeof(name), "%s%3.3i%3.3i%s", prefix, sectorX, sectorY, kSectorExtension);
    std::uint32_t size = 0;
    const std::uint8_t* file = static_cast<const std::uint8_t*>(archive->GetFile(name, &size));
    data = file;

    const char* unableToLocate = "Unable to locate resource: ";
    const char* wrongFormat = "Wrong format resource: ";
    // The message is passed to FatalError as its format string.
    auto fail = [&](const char* text) {
        std::string n(name);
        std::string message = text + n;
        return game::FatalError(message.c_str());
    };
    if (!file) return fail(unableToLocate);
    if (size < 5) return fail(wrongFormat);

    std::uint32_t magic;
    std::memcpy(&magic, file, 4);
    std::uint8_t flags = file[4];
    data = file + 5;
    if (magic != kSectorMagic) return fail(wrongFormat);

    std::uint32_t expected = 0x240D;
    hasWaterTiles = flags & 3;
    hasWater = flags & 1;
    if (hasWater) expected = 0x4815;
    if (hasWaterTiles) expected += 0x200;
    if (size != expected) return fail(wrongFormat);

    // Water material per tile.
    if (hasWater) {
        waterTiles.resize(0x100);
        const std::uint8_t* src = data + 0x4610 + (hasWaterTiles ? 0x200 : 0);
        for (int i = 0; i < 0x100; ++i) {
            std::uint16_t v;
            std::memcpy(&v, src + i * 2, 2);
            waterTiles[i] = v == 0xFFFF ? -1 : v;
        }
    } else {
        waterTiles.clear();
    }

    // Vertex positions: one or two layers of 33x33 (resized, zeroing new ones).
    vertices.resize(hasWater ? 2 * kVertices : kVertices);

    // Each vertex is 8 bytes: x and y displacement (signed bytes), height (u16), packed normal.
    const std::uint8_t* src = data;
    SectorPosition* out = vertices.data();
    for (int layer = 0; layer < (hasWater ? 2 : 1); ++layer) {
        double x0 = static_cast<double>(sx) * kSectorCells;
        double y = static_cast<double>(sy) * kSectorCells;
        int vi = 0;
        for (int row = 0; row < 33; ++row) {
            double x = x0;
            for (int col = 0; col < 33; ++col, ++vi, src += 8, ++out) {
                std::uint16_t h;
                std::memcpy(&h, src + 2, 2);
                out->z = static_cast<float>(static_cast<double>(h) * owner->heightRange * kHeightUnit);
                if (layer == 0) {
                    out->x = static_cast<float>(0.5 * static_cast<std::int8_t>(src[0]) * kOffsetScale + x);
                    out->y = static_cast<float>(0.5 * static_cast<std::int8_t>(src[1]) * kOffsetScale + y);
                } else {
                    // The water material of the vertex: the lowest priority among its tiles'.
                    const VertexTiles& around = owner->vertexTiles[vi];
                    int m = waterTiles[around.tiles[0]];
                    for (int i = 1; i < around.count; ++i) {
                        int c = waterTiles[around.tiles[i]];
                        if (c == -1) continue;
                        if (m != -1) EI_NAN_AUDIT(owner->materials[c].priority, owner->materials[m].priority);
                        if (m == -1 || owner->materials[c].priority < owner->materials[m].priority) m = c;
                    }
                    if (m == -1) {
                        out->x = static_cast<float>(x);
                        std::memset(&out->z, 0, 4);
                        out->y = static_cast<float>(y);
                    } else if (owner->materials[m].kind != 4) {
                        out->x = static_cast<float>(x);
                        out->y = static_cast<float>(y);
                    } else {
                        std::memcpy(out, &vertices[vi].x, 8);
                    }
                }
                x += 1.0;
            }
            y += 1.0;
        }
    }
    built |= 1;
    return 1;
}

// Building

void SectorData::Build() {
    records.resize(hasWater ? 2 * kVertices : kVertices);
    const std::uint8_t* tileWords = BuildRecords(*this);

    int pageCount = pages * 2 + 4;
    static_assert(sizeof(int) == 4);
    int counts[1024] = {};
    int animatedCount = CountTiles(*this, tileWords, counts);
    for (int q = 0; q < 4; ++q) {
        tileFlags[q].resize(pageCount);
        tiles[q].resize(pageCount);
        for (int k = 0; k < pageCount; ++k) {
            int c = counts[q * 256 + k];
            tileFlags[q][k].resize(c);
            tiles[q][k].resize(c * 9);
        }
    }
    animated.resize(animatedCount);
    if (hasWaterTiles) hasWaterTiles = 1;

    std::memset(counts, 0, sizeof(counts));
    FillTiles(*this, tileWords, counts);
    ComputeBounds(*this);
    owner->LightSector(this);   // terrain_lights.cpp
    built |= 2;

    if (hasWater) {
        // The water materials present in the sector, then their levels.
        for (int i = 0; i < kVertices; ++i) {
            unsigned id = records[kVertices + i].waterMaterial;
            if (id == 0xFFFF) continue;
            int k = 0;
            while (k < std::ssize(waterMaterials) && waterMaterials[k] != id) ++k;
            if (k == std::ssize(waterMaterials))
                lib::SetAtGrow(waterMaterials, std::ssize(waterMaterials), static_cast<std::uint8_t>(id));
        }
        if (hasWater) {
            std::vector<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
            for (int k = 0; k < std::ssize(waterMaterials); ++k) {
                int id = waterMaterials[k];
                std::memcpy(&levels[id], &owner->materials[id].level, 4);
            }
            ApplyWaterLevels();
        }
    }
    render::TerrainChanged(this, render::TerrainChange::Rebuilt);   // the pages kept on the GPU
}

void SectorData::ApplyWaterLevels() {
    std::vector<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
    for (;;) {
        int k = 0;
        for (; k < std::ssize(waterMaterials); ++k) {
            float v = levels[waterMaterials[k]];
            EI_NAN_AUDIT(v);
            if (v != 0.0) break;
        }
        if (k == std::ssize(waterMaterials)) return;
        unsigned id = waterMaterials[k];
        double level = levels[id];
        SectorVertex* rec = records.data() + kVertices;
        float* z = &vertices[kVertices].z;
        for (int i = 0; i < kVertices; ++i, ++rec, z += 3) {
            if (rec->waterMaterial == id) {
                rec->position.z = static_cast<float>(level + rec->position.z);
                *z = static_cast<float>(level + *z);
            }
        }
        std::memset(&levels[id], 0, 4);
        render::TerrainChanged(this, render::TerrainChange::WaterMoved);
    }
}

// Point queries

} // namespace terrain

const float* Terrain::TileUv(int rotation, int index, int page) {
    int wrapped = page % (pages + 2);
    if (wrapped >= pages && wrapped < pages + 2) return uvTablesLast[rotation] + index * 18;
    return uvTables[rotation] + index * 18;
}

int Terrain::SurfaceClass(float x, float y) {
    double unit = 1.0;
    int ix = x87::fistp(static_cast<float>(x / unit));
    int iy = x87::fistp(static_cast<float>(y / unit));
    if (ix < 0 || iy < 0 || ix >= sectorsX * 32 || iy >= sectorsY * 32) return 8;
    terrain::SectorData* sector = SectorAt(ix / 32, iy / 32, 2, 1);
    int tile = (iy % 32) / 2 * 16 + (ix % 32) / 2;
    if (!sector) return 8;
    if (sector->hasWater && sector->waterTiles[tile] != -1) {
        float water = static_cast<float>(terrain::RawHeight(this, x, y, 1));
        const double ground = terrain::RawHeight(this, x, y, 0);
        EI_NAN_AUDIT(ground, water);
        if (ground < water) return textureClass[sector->tileTextures[tile + 0x100]];
    }
    return textureClass[sector->tileTextures[tile]];
}

void Terrain::GroundAndWaterDepth(float x, float y, float* ground, float* depth) {
    float water = static_cast<float>(terrain::RawHeight(this, x, y, 1));
    double g = terrain::RawHeight(this, x, y, 0);
    *ground = static_cast<float>(g);
    if (water > g) *depth = static_cast<float>(water - g);
    else *depth = 0.0f;
}

namespace terrain {

std::uint32_t SectorData::Ensure(std::uint32_t wanted, Terrain* terrain) {
    if (wanted & built) return 1;
    if (wanted >= 2 && !(built & 2)) Build();
    if (wanted >= 4 && !(built & 4))
        Light(terrain, 0, 0, 0, 0x20, 0x20);
    return wanted & built;
}

// Construction and projection

void FreeProjectionInput() {
    if (g_freed & 1) return;
    g_freed = static_cast<std::uint8_t>(g_freed | 1);
    lib::FreeStorage(g_projectionInput);
}
void FreeProjectionOutput() {
    if (g_freed & 2) return;
    g_freed = static_cast<std::uint8_t>(g_freed | 2);
    lib::FreeStorage(g_projectionOutput);
}
void ConstructProjectionOutcodes() {
    g_projectionOutcodes.Construct();
    std::atexit(DestroyProjectionOutcodes);
}

SectorData::SectorData() {
    box = SectorBox{};
    for (SectorBox& quarter : quarters) quarter = SectorBox{};
    detail = 0;
    sx = -1;
    sy = -1;
    seamAfter = 0;
    seamBefore = 0;
    hasWater = 0;
    hasWaterTiles = 0;
    data = nullptr;
    owner = nullptr;
    pages = -1;
    perPage = -1;
    built = 0;
    lastUsed = 0;
    relightCountdown = 0;
    clipMode = kCulled;
    hasSpecular = 0;
    g_projectionInput.resize(kProjectionVertices);
    g_projectionOutput.resize(kProjectionVertices);
    g_projectionOutcodes->resize(std::ssize(g_projectionInput));
}

// Then the members, last first (each record's light list released).
SectorData::~SectorData() { Clear(); }

int SectorData::Project(CTransformPipeline* pipeline) {
    const int columnStep = detail >= 1 ? 2 : 1;
    const int rowStep = detail >= 2 ? 2 : 1;
    for (int q = 0; q < 4; ++q) {
        if (quarterMode[q] == kCulled) continue;
        const int corner = ((q / 2) * kRow + q % 2) * kQuarterCells;
        const int layers = hasWater != 0 ? 2 : 1;
        Vector4* in = g_projectionInput.data();
        for (int layer = 0; layer < layers; ++layer)
            for (int row = 0; row <= kQuarterCells; row += rowStep)
                for (int column = 0; column <= kQuarterCells; column += columnStep) {
                    const SectorPosition& p = vertices[layer * kVertices + corner + row * kRow + column];
                    std::memcpy(in, &p, sizeof(Vector3));
                    ++in;
                }
        const int count = static_cast<int>(in - g_projectionInput.data());
        const int mode = pipeline->ProjectTerrain(g_projectionInput.data(), g_projectionOutput.data(), g_projectionOutcodes->data(),
                                                  count, BoundsOf(quarters[q]));
        quarterMode[q] = mode;
        if (mode == -1 || mode == kCulled) {
            quarterMode[q] = kCulled;
            continue;
        }
        const Vector4* out = g_projectionOutput.data();
        const std::uint8_t* outcode = g_projectionOutcodes->data();
        for (int layer = 0; layer < layers; ++layer)
            for (int row = 0; row <= kQuarterCells; row += rowStep)
                for (int column = 0; column <= kQuarterCells; column += columnStep) {
                    SectorVertex& record = records[layer * kVertices + corner + row * kRow + column];
                    std::memcpy(&record.projected, out, sizeof(record.projected));
                    record.outcode = mode == kCrossing ? *outcode++ : 0;
                    ++out;
                }
    }
    return clipMode;
}

} // namespace terrain
