// The terrain draw of one sector's texture page (006BFF40; called by the terrain pass 006C6460 per
// sector and page): for each of the four quarters that is not hidden, the quarter's tiles on that
// page become 32-byte vertices and a triangle list at the quarter's level of detail (3x3, 6 or 4
// vertices per tile; tiles bordering a coarser neighbour drop the triangles of that edge), in
// buffers kept between calls (function-local statics at 007C8848..007C88C8); quarters crossing the
// view are clipped first (the clipper's slot 37), the unused vertices dropped and the indices
// renumbered; then the draw context's clipper transforms the vertices (slot 34) and the context
// draws them (slot 12) with render state 0x1D on. See docs/notes/models.md.
#include "terrain_sector.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_render.h"
#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

using terrain::SectorData;
using terrain::TileVertex;

// --- The statics (function-local in the original: constructed on first use, freed at exit) -------

// The device vertex the pages are drawn from (a D3D TL vertex, 32 bytes, filled as bytes).
struct PageVertex {
    std::uint8_t bytes[32];
};

// Engine aligned arrays (lib::AlignedArray; the original inlines their SetSize with the per-size
// allocators) and CByteArray / CWordArray (mfc::CArray layout, SetSize through MFC's afxcoll code).
lib::AlignedArray<PageVertex>& Vertices() { return *reinterpret_cast<lib::AlignedArray<PageVertex>*>(0x007C8890); }
mfc::CArray<std::uint8_t>& Flags() { return *reinterpret_cast<mfc::CArray<std::uint8_t>*>(0x007C8848); }             // a byte per vertex
mfc::CArray<std::uint16_t>& ClippedIndices() { return *reinterpret_cast<mfc::CArray<std::uint16_t>*>(0x007C8860); }  // the clipper's output
mfc::CArray<std::uint16_t>& Indices() { return *reinterpret_cast<mfc::CArray<std::uint16_t>*>(0x007C8878); }
lib::AlignedArray<std::uint8_t>& Used() { return *reinterpret_cast<lib::AlignedArray<std::uint8_t>*>(0x007C88A8); }
lib::AlignedArray<std::uint16_t>& Renumber() { return *reinterpret_cast<lib::AlignedArray<std::uint16_t>*>(0x007C88B8); }
std::uint8_t& Guards() { return *reinterpret_cast<std::uint8_t*>(0x007C88A0); }
std::uint8_t& ClipGuards() { return *reinterpret_cast<std::uint8_t*>(0x007C88C8); }

// The statics' constructors and exit handlers.
inline void __fastcall WordArraySetSize(mfc::CArray<std::uint16_t>* a, void*, int n, int g) { WordArraySetSizeOriginal(a, nullptr, n, g); }
inline void __fastcall ByteArraySetSize(mfc::CArray<std::uint8_t>* a, void*, int n, int g) { ByteArraySetSizeOriginal(a, nullptr, n, g); }
void(__cdecl* const kFreeUsed)() = reinterpret_cast<void(__cdecl*)()>(0x006C1300);
void(__cdecl* const kFreeRenumber)() = reinterpret_cast<void(__cdecl*)()>(0x006C1320);
void(__cdecl* const kDestroyFlags)() = reinterpret_cast<void(__cdecl*)()>(0x006C1340);
void(__cdecl* const kDestroyClipped)() = reinterpret_cast<void(__cdecl*)()>(0x006C1350);
void(__cdecl* const kDestroyIndices)() = reinterpret_cast<void(__cdecl*)()>(0x006C1360);
void(__cdecl* const kFreeVertices)() = reinterpret_cast<void(__cdecl*)()>(0x006C1370);

// 006C30E0 SectorData::ApplyWaterLevels (terrain_sector.cpp).
__declspec(naked) void __fastcall ApplyWaterLevels(SectorData*) EI_ORIGINAL(0x006C30E0)

// SetSize of the vertex array keeps the contents when it grows (lib::AlignedArray::SetSize). The
// size is four times the vertex count (the original's unit is 8 bytes) while the elements are 32
// bytes: the buffer is four times larger than needed, as in the original. The clipping work arrays
// drop their old contents when they grow (SetSizeDiscard).
void SizeVertices(int size) { Vertices().SetSize(size); }

void EnsureStatics() {
    if (!(Guards() & 1)) {
        Vertices() = lib::AlignedArray<PageVertex>{};
        Guards() |= 1;
        GameAtExit(kFreeVertices);
    }
    if (!(Guards() & 2)) {
        Guards() |= 2;
        WordArrayConstruct(&Indices());
        GameAtExit(kDestroyIndices);
    }
    if (!(Guards() & 4)) {
        Guards() |= 4;
        WordArrayConstruct(&ClippedIndices());
        GameAtExit(kDestroyClipped);
    }
    if (!(Guards() & 8)) {
        Guards() |= 8;
        ByteArrayConstruct(&Flags());
        GameAtExit(kDestroyFlags);
    }
}

void EnsureClipStatics() {
    if (!(ClipGuards() & 1)) {
        Renumber() = lib::AlignedArray<std::uint16_t>{};
        ClipGuards() |= 1;
        GameAtExit(kFreeRenumber);
    }
    if (!(ClipGuards() & 2)) {
        Used() = lib::AlignedArray<std::uint8_t>{};
        ClipGuards() |= 2;
        GameAtExit(kFreeUsed);
    }
}

// --- Vertices ------------------------------------------------------------------------------------

// A tile entry as the device takes it: the record's first 16 bytes, its 8 bytes at +0x11 and the
// entry's texture coordinates; the record's byte +0x10 goes to the flags array. (The original copies
// the qwords with FILD / FISTP, which is exact.)
void PutVertex(char* out, std::uint8_t* flag, const TileVertex& entry) {
    const char* record = reinterpret_cast<const char*>(entry.vertex);
    std::memcpy(out, record, 16);
    std::memcpy(out + 16, record + 0x11, 8);
    std::memcpy(out + 24, &entry.u, 8);
    *flag = static_cast<std::uint8_t>(record[0x10]);
}

// The entries of a tile (3x3, row by row) each level of detail uses.
constexpr int kLod1Entries[6] = {0, 2, 3, 5, 6, 8};
constexpr int kLod2Entries[4] = {0, 2, 6, 8};
// Vertices and triangles per tile by level of detail: {9, 6, 4} and {8, 4, 2}, read from the exe's
// tables as the original does (a level above 2 reads past them and draws nothing).
int VerticesPerTile(int lod) { return reinterpret_cast<const int*>(0x0079699C)[lod]; }
int TrianglesPerTile(int lod) { return reinterpret_cast<const int*>(0x007969A8)[lod]; }

void FillVertices(int lod, const TileVertex* entries, int tiles, int vertexCount) {
    char* out = reinterpret_cast<char*>(Vertices().data);
    auto* flags = Flags().data;
    if (lod == 0) {
        for (int i = 0; i < vertexCount; ++i) PutVertex(out + i * 32, flags + i, entries[i]);
    } else if (lod == 1) {
        for (int t = 0; t < tiles; ++t, entries += 9)
            for (int k = 0; k < 6; ++k, out += 32, ++flags) PutVertex(out, flags, entries[kLod1Entries[k]]);
    } else if (lod == 2) {
        for (int t = 0; t < tiles; ++t, entries += 9)
            for (int k = 0; k < 4; ++k, out += 32, ++flags) PutVertex(out, flags, entries[kLod2Entries[k]]);
    }
}

// --- Triangles -----------------------------------------------------------------------------------

// Index patterns relative to the tile's first vertex. Level 0 (3x3 vertices, 8 triangles) and its
// variants without the triangles along the edge to a coarser quarter (flags bit 0 / bit 1);
// level 1 (6 vertices: rows 0, 1 / 1, 2 of columns 0 and 2) and its variants (bits 2 / 3);
// level 2 (the four corners).
constexpr std::uint16_t kLod0[24] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};
constexpr std::uint16_t kLod0Edge1[21] = {0, 2, 4, 0, 4, 3, 2, 5, 4, 3, 4, 7, 3, 7, 6, 4, 5, 8, 4, 8, 7};
constexpr std::uint16_t kLod0Edge2[21] = {0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 3, 4, 6, 4, 8, 6, 4, 5, 8};
constexpr std::uint16_t kLod1[12] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
constexpr std::uint16_t kLod1Edge4[9] = {0, 1, 3, 0, 3, 4, 3, 5, 4};
constexpr std::uint16_t kLod1Edge8[9] = {0, 1, 2, 1, 5, 2, 2, 5, 4};
constexpr std::uint16_t kLod2[6] = {0, 1, 3, 0, 3, 2};

template <int N>
void PutTriangles(std::uint16_t* indices, int& count, int base, const std::uint16_t (&pattern)[N]) {
    for (int k = 0; k < N; ++k) indices[count + k] = static_cast<std::uint16_t>(base + pattern[k]);
    count += N;
}

// Fills the index list; returns the index count and sets `vertexCount` to the vertices used.
int FillIndices(const SectorData* s, int lod, int tiles, const std::uint8_t* edges, int& vertexCount) {
    auto* indices = Indices().data;
    int count = 0;
    int base = 0;
    bool seams = s->seamBefore != 0 || s->seamAfter != 0;
    if (lod == 2) {
        for (int t = 0; t < tiles; ++t, base += 4) PutTriangles(indices, count, base, kLod2);
    } else if (lod == 1) {
        for (int t = 0; t < tiles; ++t, base += 6) {
            std::uint8_t edge = seams ? edges[t] : 0;
            if (seams && s->seamBefore != 0 && (edge & 4)) PutTriangles(indices, count, base, kLod1Edge4);
            else if (seams && s->seamAfter != 0 && (edge & 8)) PutTriangles(indices, count, base, kLod1Edge8);
            else PutTriangles(indices, count, base, kLod1);
        }
    } else if (lod == 0) {
        for (int t = 0; t < tiles; ++t, base += 9) {
            std::uint8_t edge = seams ? edges[t] : 0;
            if (seams && s->seamBefore != 0 && (edge & 1)) PutTriangles(indices, count, base, kLod0Edge1);
            else if (seams && s->seamAfter != 0 && (edge & 2)) PutTriangles(indices, count, base, kLod0Edge2);
            else PutTriangles(indices, count, base, kLod0);
        }
    }
    vertexCount = base;
    return count;
}

// --- Clipping and submission ---------------------------------------------------------------------

// After clipping: only the vertices the clipped triangles use are kept (in order), the indices
// renumbered to them. Returns the new vertex count.
int CompactClipped(int vertexCount, int indexCount) {
    EnsureClipStatics();
    Renumber().SetSizeDiscard(vertexCount);
    Used().SetSizeDiscard(vertexCount);
    auto* used = Used().data;
    std::memset(used, 0, static_cast<std::size_t>(vertexCount));
    auto* clipped = ClippedIndices().data;
    for (int i = 0; i < indexCount; ++i) used[clipped[i]] = 0xFF;
    char* vertices = reinterpret_cast<char*>(Vertices().data);
    auto* renumber = Renumber().data;
    int kept = 0;
    for (int i = 0; i < vertexCount; ++i) {
        if (!used[i]) continue;
        std::memmove(vertices + kept * 32, vertices + i * 32, 32);
        renumber[i] = static_cast<std::uint16_t>(kept);
        ++kept;
    }
    for (int i = 0; i < indexCount; ++i) clipped[i] = renumber[clipped[i]];
    return kept;
}

void* DrawDevice() { return *reinterpret_cast<void**>(*reinterpret_cast<char**>(0x007AF080) + 0x54); }
void SetRenderState(int state, int value) {
    void* device = DrawDevice();
    auto fn = reinterpret_cast<long(__stdcall*)(void*, int, int)>((*reinterpret_cast<void***>(device))[20]);
    fn(device, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), static_cast<std::uint32_t>(value));
}

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*reinterpret_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

constexpr int kRenderState = 0x1D;
constexpr int kClipperClip = 37;      // (vertices, flags, indices, out indices, &vertices, &indices)
constexpr int kClipperProcess = 34;   // (vertices, count)
constexpr int kContextDraw = 12;      // (vertices, count, indices, index count) -> 0 on failure

} // namespace

// 006BFF40. Returns 1, or 0 when the context fails to draw.
int __fastcall DrawSectorPage(SectorData* s, void*, void* context, void* /*unused*/, int page, int /*unused*/) {
    void* clipper = *reinterpret_cast<void**>(*reinterpret_cast<char**>(static_cast<char*>(context) + 8) + 0xC);
    if (s->clipMode == 2) return 1;
    EnsureStatics();
    if (s->hasWater) ApplyWaterLevels(s);
    // The new renderer (src/render) takes the page; with "noterrain" the old path stops here. The
    // water levels above stay: they move the water surface that height queries read.
    if (render::Active()) render::TerrainPage(s, context, page);
    if (!render::DrawOldTerrain()) return 1;

    for (int quarter = 0; quarter < 4; ++quarter) {
        int mode = s->quarterMode[quarter];
        if (mode == 2) continue;   // hidden
        bool clip = mode == 1;
        const terrain::TileList& list = s->tiles[quarter].data[page];
        int tiles = list.size / 9;
        if (tiles == 0) continue;
        int lod = s->detail;
        int vertices = VerticesPerTile(lod) * tiles;
        int indexCapacity = TrianglesPerTile(lod) * tiles * 3;
        SizeVertices(vertices * 4);
        ByteArraySetSize(&Flags(), nullptr, vertices * 4, -1);
        WordArraySetSize(&Indices(), nullptr, indexCapacity, -1);
        WordArraySetSize(&ClippedIndices(), nullptr, indexCapacity * 4, -1);
        FillVertices(lod, list.data, tiles, vertices);

        // The tile edge flags are only looked at when a neighbouring quarter is coarser.
        const std::uint8_t* edges = nullptr;
        if (lod != 2 && (s->seamBefore != 0 || s->seamAfter != 0)) edges = s->tileFlags[quarter].data[page].data;
        int vertexCount = 0;
        int indexCount = FillIndices(s, lod, tiles, edges, vertexCount);
        if (!(indexCount > 0)) continue;
        if (clip) {
            ThisCall<void>(clipper, kClipperClip, Vertices().data, Flags().data, Indices().data, ClippedIndices().data,
                           &vertexCount, &indexCount);
            vertexCount = CompactClipped(vertexCount, indexCount);
        }
        if (!(indexCount > 0)) continue;
        ThisCall<void>(clipper, kClipperProcess, Vertices().data, vertexCount);
        SetRenderState(kRenderState, 1);
        std::uint16_t* indices = clip ? ClippedIndices().data : Indices().data;
        if (!ThisCall<int>(context, kContextDraw, Vertices().data, vertexCount, indices, indexCount)) return 0;
        SetRenderState(kRenderState, 0);
    }
    return 1;
}
EI_REPLACE(0x006BFF40, DrawSectorPage);
