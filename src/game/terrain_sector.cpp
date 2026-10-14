// Terrain sectors: loading (006C13A0), building (006C1C00), water levels (006C30E0) and the small
// lookups around them. See terrain_sector.h for the layout.
//
// Floating point: compiled with precise semantics. Values the original keeps on the FPU stack are
// `double` here; every store the original makes to a float goes through Store(). Constants it
// multiplies at run time are read from the exe (rule 9: Rebirth may patch data).
#include "terrain_sector.h"
#include "model.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "random.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"

#include <cstring>
#include "originals_library.h"
#include "originals_world.h"

#pragma float_control(precise, on)

namespace terrain {
namespace {

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

double GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The constants of this code (all floats in .rdata).
double One() { return GameFloat(0x00744160); }          // 1: vertex spacing
double SectorCells() { return GameFloat(0x0073CA90); }  // 32
double Half() { return GameFloat(0x0073B86C); }         // 0.5
double MinusHalf() { return GameFloat(0x0073CDC8); }    // -0.5
double OffsetScale() { return GameFloat(0x00744190); }  // 1/126: vertex displacement per unit
double HeightUnit() { return GameFloat(0x0073FD84); }   // 1/65534 (37800100)
double NormalScale() { return GameFloat(0x0074040C); }  // 0.001
double NormalBias() { return GameFloat(0x0073C34C); }   // 1000
double VOne() { return GameFloat(0x0073B84C); }         // 1: v = 1 - v
double Zero() { return GameFloat(0x0073B858); }         // 0

constexpr std::uint32_t kSectorMagic = 0xCF4BF774;
constexpr int kVertices = 33 * 33;      // per layer

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// Element helpers of the original (not rewritten; DestroyRecords is in terrain_lights.cpp).
__declspec(naked) void __stdcall ConstructFlagArrays(FlagArray*, int) EI_ORIGINAL(0x006C4F90)
__declspec(naked) void __stdcall DestroyFlagArrays(FlagArray*, int) EI_ORIGINAL(0x006C4F60)
__declspec(naked) void __stdcall ConstructTileLists(TileList*, int) EI_ORIGINAL(0x006C5050)
__declspec(naked) void __stdcall DestroyTileLists(TileList*, int) EI_ORIGINAL(0x006C5010)

struct MfcOps {
    void SetSizeByte(int size, int growBy);     // 006FF64A CByteArray::SetSize
    void SetAtGrowByte(int index, std::uint8_t value);  // 006FF7DC CByteArray::SetAtGrow
};
void MfcOps::SetSizeByte(int n, int g) { ByteArraySetSizeOriginal(this, nullptr, n, g); }
__declspec(naked) void MfcOps::SetAtGrowByte(int, std::uint8_t) EI_ORIGINAL(0x006FF7DC)

} // namespace

// The sector's CArray element policies. The records start zeroed (006C4F10, inlined) and release
// their lights when dropped (006C4EE0); the flag arrays and tile lists go through the original
// helpers, which match the loops the original inlines in their SetSize(0) (the CByteArray
// destructor through its vtable, 006C5120 per tile list).
void RecordElements::Construct(SectorVertex* records, int count) {
    std::memset(static_cast<void*>(records), 0, sizeof(SectorVertex) * count);
}
void RecordElements::Destruct(SectorVertex* records, int count) { DestroyRecords(records, count); }
void FlagArrayElements::Construct(FlagArray* arrays, int count) { ConstructFlagArrays(arrays, count); }
void FlagArrayElements::Destruct(FlagArray* arrays, int count) { DestroyFlagArrays(arrays, count); }
void TileListElements::Construct(TileList* lists, int count) { ConstructTileLists(lists, count); }
void TileListElements::Destruct(TileList* lists, int count) { DestroyTileLists(lists, count); }

namespace {

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

int ClassOf(const TerrainFields* owner, int texture) {
    int cls = owner->textureClass[texture];
    if (cls >= owner->perPage * owner->pages) cls = 0xF;
    return cls;
}

bool IsAnimated(const TerrainFields* owner, int texture) {
    return texture < owner->perPage * owner->pages && owner->textureFrames[texture] != 0;
}

} // namespace

__declspec(naked) void SectorData::BuildAtOriginal() EI_ORIGINAL(0x006C1C00)
__declspec(naked) void SectorData::LightAtOriginal(CLight*, TerrainFields*, int, int, int, int, int) EI_ORIGINAL(0x006C3A00)

const float* TerrainFields::TileUv(int rotation, int index, int page) {
    int wrapped = page % (pages + 2);
    if (wrapped >= pages && wrapped < pages + 2) return uvTablesLast[rotation] + index * 18;
    return uvTables[rotation] + index * 18;
}

// ---------------------------------------------------------------------------------------------
// Loading

int SectorData::Load(void* archive, int sectorX, int sectorY, int pageCount, int textures, const char* prefix,
                     TerrainFields* terrain) {
    Clear();
    owner = terrain;
    sx = sectorX;
    pages = pageCount;
    sy = sectorY;
    perPage = textures;

    char name[0x100];
    gamecrt::sprintf(name, "%s%3.3i%3.3i%s", prefix, sectorX, sectorY, *reinterpret_cast<const char* const*>(0x00744164));
    using GetFileFn = void*(__thiscall*)(void*, const char*, std::uint32_t*);
    std::uint32_t size = 0;
    const std::uint8_t* file = static_cast<const std::uint8_t*>(
        (*reinterpret_cast<GetFileFn* const*>(archive))[5](archive, name, &size));
    data = file;

    const char* unableToLocate = reinterpret_cast<const char*>(0x00791774);   // "Unable to locate resource "
    const char* wrongFormat = reinterpret_cast<const char*>(0x0079175C);      // "Wrong format resource "
    // The message is passed to FatalError as its format string, as the original does.
    auto fail = [&](const char* text) {
        mfc::CString n(name);
        mfc::CString message = mfc::operator+(text, n);
        return game::FatalError(static_cast<const char*>(message));
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
        waterTiles.SetSize(0x100);
        const std::uint8_t* src = data + 0x4610 + (hasWaterTiles ? 0x200 : 0);
        for (int i = 0; i < 0x100; ++i) {
            std::uint16_t v;
            std::memcpy(&v, src + i * 2, 2);
            waterTiles.data[i] = v == 0xFFFF ? -1 : v;
        }
    } else {
        waterTiles.RemoveAll();
    }

    // Vertex positions: one or two layers of 33x33 (CArray<Vector3>::SetSize, zeroing new ones).
    vertices.SetSize(hasWater ? 2 * kVertices : kVertices);

    // Each vertex is 8 bytes: x and y displacement (signed bytes), height (u16), packed normal.
    const std::uint8_t* src = data;
    float* out = vertices.data->xyz;
    for (int layer = 0; layer < (hasWater ? 2 : 1); ++layer) {
        double x0 = static_cast<double>(sx) * One() * SectorCells();
        double y = static_cast<double>(sy) * One() * SectorCells();
        int vi = 0;
        for (int row = 0; row < 33; ++row) {
            double x = x0;
            for (int col = 0; col < 33; ++col, ++vi, src += 8, out += 3) {
                std::uint16_t h;
                std::memcpy(&h, src + 2, 2);
                out[2] = Store(static_cast<double>(h) * owner->heightScale * HeightUnit());
                if (layer == 0) {
                    out[0] = Store(One() * Half() * static_cast<std::int8_t>(src[0]) * OffsetScale() + x);
                    out[1] = Store(One() * Half() * static_cast<std::int8_t>(src[1]) * OffsetScale() + y);
                } else {
                    // The water material of the vertex: the lowest priority among its tiles'.
                    const VertexTiles& around = owner->vertexTiles[vi];
                    int m = waterTiles.data[around.tiles[0]];
                    for (int i = 1; i < around.count; ++i) {
                        int c = waterTiles.data[around.tiles[i]];
                        if (c == -1) continue;
                        if (m == -1 || !(owner->materials[c].priority >= owner->materials[m].priority)) m = c;
                    }
                    if (m == -1) {
                        out[0] = Store(x);
                        std::memset(out + 2, 0, 4);
                        out[1] = Store(y);
                    } else if (owner->materials[m].kind != 4) {
                        out[0] = Store(x);
                        out[1] = Store(y);
                    } else {
                        std::memcpy(out, vertices.data[vi].xyz, 8);
                    }
                }
                x += One();
            }
            y += One();
        }
    }
    built |= 1;
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Building

namespace {

// Vertex records: water material (water layer), normals, height range. Returns the tile words.
const std::uint8_t* BuildRecords(SectorData& s) {
    const std::uint8_t* tiles = s.data;
    const std::uint8_t* src = s.data;
    SectorVertex* rec = s.records.data;
    const float* vin = s.vertices.data->xyz;
    std::memcpy(&s.minZ, "\xca\xf2\x49\x71", 4);    // 1e30
    s.maxZ = 0.0f;
    for (int layer = 0; layer < (s.hasWater ? 2 : 1); ++layer) {
        if (layer == 1) tiles += 0x2208;
        for (int vi = 0; vi < kVertices; ++vi, ++rec, vin += 3, src += 8) {
            // Branch-free selects on the FCOMP status (C0): the minimum keeps its value when it is
            // below z or unordered, the maximum when z is below it or unordered.
            float z = vin[2];
            if (!(s.minZ < z) && !x87::IsNaN(s.minZ) && !x87::IsNaN(z)) s.minZ = z;
            if (!(z < s.maxZ) && !x87::IsNaN(z) && !x87::IsNaN(s.maxZ)) s.maxZ = z;

            rec->materialByte = 0xFF;
            rec->materialColour = {0.0f, 0.0f, 0.0f};
            rec->waterMaterial = 0xFFFF;
            if (layer == 1) {
                // The first water material among the vertex's tiles.
                const VertexTiles& around = s.owner->vertexTiles[vi];
                int m = s.waterTiles.data[around.tiles[0]];
                for (int i = 1; m == -1 && i < around.count; ++i) m = s.waterTiles.data[around.tiles[i]];
                rec->waterMaterial = static_cast<std::uint16_t>(m);
                if (m != -1) {
                    const TerrainMaterial& mat = s.owner->materials[m];
                    rec->materialByte = mat.opacityByte;
                    rec->materialColour = mat.colour;
                }
                std::memcpy(&rec->position, vin, 12);
                (rec - kVertices)->depth = Store(static_cast<double>(vin[2]) - vin[2 - kVertices * 3]);
            }
            std::uint32_t n;
            std::memcpy(&n, src + 4, 4);
            rec->normal.z = Store(x87::FromUnsigned(n >> 22) * NormalScale());
            rec->normal.x = Store((static_cast<double>(static_cast<int>((n >> 11) & 0x7FF)) - NormalBias()) * NormalScale());
            rec->normal.y = Store((static_cast<double>(static_cast<int>(n & 0x7FF)) - NormalBias()) * NormalScale());
        }
    }
    return tiles + 0x2208;
}

// Counts the tiles per quarter and texture page (counts[quarter * 256 + page]), stores the tile
// textures and rotations and the texture class counts. Returns the number of animated tiles.
int CountTiles(SectorData& s, const std::uint8_t* tiles, int* counts) {
    const TerrainFields* owner = s.owner;
    int animated = 0;
    s.classCounts.SetSize(16);
    std::memset(s.classCounts.data, 0, 16 * 4);
    s.tileTextures.SetSize(s.hasWaterTiles ? 0x200 : 0x100);

    const std::uint8_t* src = tiles;
    int* out = s.tileTextures.data;
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
            ++s.classCounts.data[ClassOf(owner, texture)];
        }
    }
    if (s.hasWaterTiles) {
        const int* water = s.waterTiles.data;
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
                ++s.classCounts.data[ClassOf(owner, texture)];
            }
        }
    }
    return animated;
}

// Fills the tile lists: 9 entries per tile (3x3 vertices with texture coordinates), its edge
// flags, and the animated tiles (random start frame).
void FillTiles(SectorData& s, const std::uint8_t* tiles, int* counts) {
    TerrainFields* owner = s.owner;
    const std::uint8_t* src = tiles;
    const int* water = s.waterTiles.data;
    int animatedIndex = 0;
    int base = 0, pageAdd = 0;
    for (int layer = 0; layer < (s.hasWaterTiles ? 2 : 1); ++layer) {
        if (layer == 1) {
            pageAdd = s.pages + 2;
            base = kVertices;
            water = s.waterTiles.data;
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
                TileVertex* entry = s.tiles[quarter].data[page].data + count * 9;
                SectorVertex* rec = s.records.data + vi;
                if (texture < owner->perPage * owner->pages) {
                    int frames = owner->textureFrames[texture];
                    if (frames != 0) {
                        AnimatedTile& a = s.animated.data[animatedIndex++];
                        a.frameBase = index;
                        a.vertices = entry;
                        a.rotation = rotation;
                        std::uint32_t r = GlobalRandom().Next();
                        a.frames = frames;
                        a.frame = static_cast<int>(r % static_cast<std::uint32_t>(frames));
                        a.speed = reinterpret_cast<const int*>(0x00796960)[ClassOf(owner, texture)];
                        uv = owner->TileUv(rotation, a.frame + index, 0);
                    }
                }
                for (int r3 = 0; r3 < 3; ++r3, rec += 33) {
                    for (int c3 = 0; c3 < 3; ++c3, ++entry) {
                        std::memcpy(&entry->u, &uv[r3 * 3 + c3], 4);
                        entry->vertex = rec + c3;
                        entry->v = Store(VOne() - uv[r3 * 3 + c3 + 9]);
                    }
                }
                std::uint8_t& flags = s.tileFlags[quarter].data[page].data[count];
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
    double size = One() * SectorCells();
    float x0 = Store(s.sx * size);
    float y0 = Store(s.sy * size);
    double half = size * Half();
    s.maxZ = Store(One() + s.maxZ);
    double low = static_cast<double>(s.minZ) - One();
    s.minZ = Store(low);
    double cz = (static_cast<double>(s.maxZ) - low) * Half() + low;
    float centreZ = Store(cz);
    s.box.centre.x = Store(half + x0);
    s.box.centre.y = Store(half + y0);
    s.box.centre.z = centreZ;
    s.box.low.x = Store(-half);
    s.box.low.y = Store(-half);
    s.box.low.z = Store(s.minZ - cz);
    s.box.high.x = Store(half);
    s.box.high.y = Store(half);
    s.box.high.z = Store(s.maxZ - cz);
    s.box.flags |= 2;
    double q = half * Half();
    double r = half * MinusHalf();
    SectorBox* box = s.quarters;
    for (int i = 0; i < 2; ++i) {
        float by = Store((i + Half()) * half + y0);
        for (int j = 0; j < 2; ++j, ++box) {
            box->centre.x = Store((j + Half()) * half + x0);
            box->centre.y = by;
            box->centre.z = centreZ;
            box->low.x = Store(r);
            box->low.y = Store(r);
            box->low.z = Store(s.minZ - cz);
            box->high.x = Store(q);
            box->high.y = Store(q);
            box->high.z = Store(s.maxZ - cz);
            box->flags |= 2;
        }
    }
}

} // namespace

void SectorData::Build() {
    records.SetSize(hasWater ? 2 * kVertices : kVertices);
    const std::uint8_t* tileWords = BuildRecords(*this);

    int pageCount = pages * 2 + 4;
    static_assert(sizeof(int) == 4);
    int counts[1024] = {};
    int animatedCount = CountTiles(*this, tileWords, counts);
    for (int q = 0; q < 4; ++q) {
        tileFlags[q].SetSize(pageCount);
        tiles[q].SetSize(pageCount);
        for (int k = 0; k < pageCount; ++k) {
            int c = counts[q * 256 + k];
            reinterpret_cast<MfcOps*>(&tileFlags[q].data[k])->SetSizeByte(c, -1);
            tiles[q].data[k].SetSize(c * 9);
        }
    }
    animated.SetSize(animatedCount);
    if (hasWaterTiles) hasWaterTiles = 1;

    std::memset(counts, 0, sizeof(counts));
    FillTiles(*this, tileWords, counts);
    ComputeBounds(*this);
    reinterpret_cast<Terrain*>(owner)->LightSector(this);   // terrain_lights.cpp
    built |= 2;

    if (hasWater) {
        // The water materials present in the sector, then their levels.
        for (int i = 0; i < kVertices; ++i) {
            unsigned id = records.data[kVertices + i].waterMaterial;
            if (id == 0xFFFF) continue;
            int k = 0;
            while (k < waterMaterials.size && waterMaterials.data[k] != id) ++k;
            if (k == waterMaterials.size)
                reinterpret_cast<MfcOps*>(&waterMaterials)->SetAtGrowByte(waterMaterials.size, static_cast<std::uint8_t>(id));
        }
        if (hasWater) {
            mfc::CArray<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
            for (int k = 0; k < waterMaterials.size; ++k) {
                int id = waterMaterials.data[k];
                std::memcpy(&levels.data[id], &owner->materials[id].level, 4);
            }
            ApplyWaterLevels();
        }
    }
}

void SectorData::ApplyWaterLevels() {
    mfc::CArray<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
    for (;;) {
        int k = 0;
        for (; k < waterMaterials.size; ++k) {
            float v = levels.data[waterMaterials.data[k]];
            if (v < Zero() || v > Zero()) break;     // FCOMP; TEST AH,0x40: equal or unordered goes on
        }
        if (k == waterMaterials.size) return;
        unsigned id = waterMaterials.data[k];
        double level = levels.data[id];
        SectorVertex* rec = records.data + kVertices;
        float* z = &vertices.data[kVertices].xyz[2];
        for (int i = 0; i < kVertices; ++i, ++rec, z += 3) {
            if (rec->waterMaterial == id) {
                rec->position.z = Store(level + rec->position.z);
                *z = Store(level + *z);
            }
        }
        std::memset(&levels.data[id], 0, 4);
    }
}

// ---------------------------------------------------------------------------------------------
// Point queries

namespace {
double RawHeight(void* terrain, float x, float y, int layer) {
    return TerrainHeightAtOriginal(static_cast<Terrain*>(terrain), nullptr, x, y, layer);   // 006C8680, ST0 used unrounded
}
} // namespace

int TerrainFields::SurfaceClass(float x, float y) {
    double unit = GameFloat(0x007441D0);
    int ix = x87::fistp(Store(x / unit));
    int iy = x87::fistp(Store(y / unit));
    if (ix < 0 || iy < 0 || ix >= sectorsX * 32 || iy >= sectorsY * 32) return 8;
    auto* sector = reinterpret_cast<SectorData*>(reinterpret_cast<Terrain*>(this)->SectorAt(ix / 32, iy / 32, 2, 1));
    int tile = (iy % 32) / 2 * 16 + (ix % 32) / 2;
    if (!sector) return 8;
    if (sector->hasWater && sector->waterTiles.data[tile] != -1) {
        float water = Store(RawHeight(this, x, y, 1));
        if (!(RawHeight(this, x, y, 0) >= water)) return textureClass[sector->tileTextures.data[tile + 0x100]];
    }
    return textureClass[sector->tileTextures.data[tile]];
}

} // namespace terrain

void Terrain::GroundAndWaterDepth(float x, float y, float* ground, float* depth) {
    float water = terrain::Store(terrain::RawHeight(this, x, y, 1));
    double g = terrain::RawHeight(this, x, y, 0);
    *ground = terrain::Store(g);
    if (water > g) *depth = terrain::Store(water - g);
    else *depth = 0.0f;
}

namespace terrain {

std::uint32_t SectorData::Ensure(std::uint32_t wanted, TerrainFields* terrain) {
    if (wanted & built) return 1;
    if (wanted >= 2 && !(built & 2)) BuildAtOriginal();
    if (wanted >= 4 && !(built & 4))
        LightAtOriginal(terrain->sun, terrain, 0, 0, 0, 0x20, 0x20);
    return wanted & built;
}

EI_REPLACE_METHOD(0x006C13A0, SectorData::Load);
EI_REPLACE_METHOD(0x006C1C00, SectorData::Build);
EI_REPLACE_METHOD(0x006C30E0, SectorData::ApplyWaterLevels);
EI_REPLACE_METHOD(0x006C3DD0, SectorData::Ensure);
EI_REPLACE_METHOD(0x006C53C0, TerrainFields::TileUv);
EI_REPLACE_METHOD(0x006C8D40, TerrainFields::SurfaceClass);
EI_REPLACE_METHOD(0x006C8930, Terrain::GroundAndWaterDepth);

// ---------------------------------------------------------------------------------------------
// Construction and projection (A-H18)

namespace {

// The projection buffers every sector shares (allocated by the first constructor): positions in,
// projected positions and outcodes out (16-byte vertices; the input's fourth float is not written).
using ProjectionVertex = Vector4;
static_assert(sizeof(ProjectionVertex) == 16);
using ProjectionBuffer = lib::AlignedArray<ProjectionVertex>;
ProjectionBuffer& ProjectionInput() { return *reinterpret_cast<ProjectionBuffer*>(std::uintptr_t(0x007C88F8)); }
ProjectionBuffer& ProjectionOutput() { return *reinterpret_cast<ProjectionBuffer*>(std::uintptr_t(0x007C88E8)); }
FlagArray& ProjectionOutcodes() { return *reinterpret_cast<FlagArray*>(std::uintptr_t(0x007C88D0)); }
constexpr int kProjectionVertices = 0x330;

// The vtables of the sector's CArray instantiations.
constexpr std::uintptr_t kByteArrayVtable = std::uintptr_t(0x00746560);
constexpr std::uintptr_t kPositionArrayVtable = std::uintptr_t(0x0073C120);
constexpr std::uintptr_t kRecordArrayVtable = std::uintptr_t(0x00744184);
constexpr std::uintptr_t kFlagArrayArrayVtable = std::uintptr_t(0x007441A8);
constexpr std::uintptr_t kTileListArrayVtable = std::uintptr_t(0x007441B4);

constexpr int kQuarterCells = 16;
constexpr int kRow = 33;

// A quarter's box is a ClipBounds followed by three unused dwords.
static_assert(offsetof(SectorBox, low) == offsetof(ClipBounds, boxMin) &&
              offsetof(SectorBox, high) == offsetof(ClipBounds, boxMax) &&
              offsetof(SectorBox, centre) == offsetof(ClipBounds, center));

} // namespace

SectorData* SectorData::Construct() {
    box = SectorBox{};
    for (SectorBox& quarter : quarters) quarter = SectorBox{};
    waterMaterials.Construct(kByteArrayVtable);
    vertices.Construct(kPositionArrayVtable);
    records.Construct(kRecordArrayVtable);
    tileTextures.Construct();
    waterTiles.Construct();
    for (auto& flags : tileFlags) flags.Construct(kFlagArrayArrayVtable);
    for (auto& lists : tiles) lists.Construct(kTileListArrayVtable);
    animated.Construct();
    classCounts.Construct();
    unknown_170 = 0;
    sx = -1;
    sy = -1;
    unknown_178 = 0;
    unknown_174 = 0;
    hasWater = 0;
    hasWaterTiles = 0;
    data = nullptr;
    owner = nullptr;
    pages = -1;
    perPage = -1;
    built = 0;
    lastUsed = 0;
    relightCountdown = 0;
    unknown_060 = kCulled;
    unknown_38C = 0;
    unknown_390 = 0;
    hasSpecular = 0;
    ProjectionInput().SetSize(kProjectionVertices);
    ProjectionOutput().SetSize(kProjectionVertices);
    ByteArraySetSizeOriginal(&ProjectionOutcodes(), nullptr, ProjectionInput().size, -1);
    return this;
}
EI_REPLACE_METHOD(0x006BF5F0, SectorData::Construct);

void SectorData::DestructBody() {
    Clear();
    classCounts.FreeData();
    animated.FreeData();
    for (int q = 3; q >= 0; --q) {
        tiles[q].vtable = reinterpret_cast<void*>(kTileListArrayVtable);
        tiles[q].FreeData();
    }
    for (int q = 3; q >= 0; --q) {
        tileFlags[q].vtable = reinterpret_cast<void*>(kFlagArrayArrayVtable);
        tileFlags[q].FreeData();
    }
    waterTiles.FreeData();
    tileTextures.FreeData();
    records.vtable = reinterpret_cast<void*>(kRecordArrayVtable);
    records.FreeData();   // each record's light list released (the original inlines 006C4EE0's loop)
    vertices.vtable = reinterpret_cast<void*>(kPositionArrayVtable);
    vertices.FreeData();
    waterMaterials.vtable = reinterpret_cast<void*>(kByteArrayVtable);   // ~CByteArray (006FF61D)
    waterMaterials.FreeData();
}
EI_REPLACE_METHOD(0x006BF8A0, SectorData::DestructBody);

int SectorData::Project(CTransformPipeline* pipeline) {
    const int columnStep = unknown_170 >= 1 ? 2 : 1;
    const int rowStep = unknown_170 >= 2 ? 2 : 1;
    for (int q = 0; q < 4; ++q) {
        if (quarterMode[q] == kCulled) continue;
        const int corner = ((q / 2) * kRow + q % 2) * kQuarterCells;
        const int layers = hasWater != 0 ? 2 : 1;
        ProjectionVertex* in = ProjectionInput().data;
        for (int layer = 0; layer < layers; ++layer)
            for (int row = 0; row <= kQuarterCells; row += rowStep)
                for (int column = 0; column <= kQuarterCells; column += columnStep) {
                    const SectorPosition& p = vertices.data[layer * kVertices + corner + row * kRow + column];
                    std::memcpy(in, p.xyz, sizeof(p.xyz));
                    ++in;
                }
        const int count = static_cast<int>(in - ProjectionInput().data);
        const int mode = pipeline->ProjectTerrain(ProjectionInput().data, ProjectionOutput().data, ProjectionOutcodes().data,
                                                  count, BoundsOf(quarters[q]));
        quarterMode[q] = mode;
        if (mode == -1 || mode == kCulled) {
            quarterMode[q] = kCulled;
            continue;
        }
        const ProjectionVertex* out = ProjectionOutput().data;
        const std::uint8_t* outcode = ProjectionOutcodes().data;
        for (int layer = 0; layer < layers; ++layer)
            for (int row = 0; row <= kQuarterCells; row += rowStep)
                for (int column = 0; column <= kQuarterCells; column += columnStep) {
                    SectorVertex& record = records.data[layer * kVertices + corner + row * kRow + column];
                    std::memcpy(&record.projected, out, sizeof(record.projected));
                    record.outcode = mode == kCrossing ? *outcode++ : 0;
                    ++out;
                }
    }
    return unknown_060;
}
EI_REPLACE_METHOD(0x006BFC80, SectorData::Project);

} // namespace terrain

