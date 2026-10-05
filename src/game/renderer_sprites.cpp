// The renderer's sprite draw (00502CA0, virtual slot 17 of the renderer at 007AF060, vtable
// 0073D93C) with its helpers 00503680 (indexed draw), 00503710 (a radix sort pass) and 005037D0 (a
// merge sort pass). Every point (x, y, z and a size) becomes a screen-facing quad: the sprite's
// colour and texture rectangle go to four TL vertices, and the pipeline's transform object (slot 24)
// places the corners and sets their outcodes. Batches of up to 511 sprites are sorted back to front
// by the first corner's z (sprites with a negative z are dropped) and drawn as two triangles each;
// when the transform reports that some corners are off screen, every triangle goes through the
// screen clipper (0042D910) first. Particles (004976B0) draw through it, with additive blending when
// their source asks for it. Buffers are function-local statics (007AEF60..007AEFF8). See
// docs/notes/models.md ("Renderer").
#include "clip.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "render/render.h"
#include "ei/x87.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>
#include <utility>

// One record of the back-to-front sort (16 bytes, 007AEFD8).
struct SpriteSortRecord {
    std::int32_t key;             // the first corner's z, as bits (never negative here)
    SpriteSortRecord* next;       // the radix pass's bucket list
    TLVertex* corners;            // the sprite's four corners (in the copy at 007AEFE8)
    std::uint32_t outcodes;       // their four outcode bytes
};
static_assert(sizeof(SpriteSortRecord) == 16);

void __fastcall SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending);
void __fastcall SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count);

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// --- The arguments -------------------------------------------------------------------------------

struct SpritePoint {          // 16 bytes: the centre and the size
    float x, y, z, size;
};
struct Sprite {               // 8 bytes
    std::uint32_t color;      // ARGB
    const float* uv;          // the texture rectangle: u0, v0, u1, v1
};
struct PointArray {           // an aligned array (data, size, ...)
    const SpritePoint* data;
    int size;
};
struct SpriteArray {          // an MFC-style array (vtable, data, size, ...)
    void* vtable;
    const Sprite* data;
    int size;
};

// --- The statics ---------------------------------------------------------------------------------

struct AlignedArray {         // the engine's aligned array: data (32-byte aligned), size, capacity, growth
    char* data;
    int size;
    int capacity;
    int growBy;
};
struct MfcArray {             // CByteArray / CWordArray
    void* vtable;
    void* data;
    int size;
    int capacity;
    int growBy;
};

AlignedArray& Unused() { return *reinterpret_cast<AlignedArray*>(0x007AEFA0); }      // 16 bytes each; sized only
AlignedArray& Corners() { return *reinterpret_cast<AlignedArray*>(0x007AEF90); }     // TL vertices, 4 per sprite
MfcArray& Outcodes() { return *reinterpret_cast<MfcArray*>(0x007AEF78); }            // a byte per corner
MfcArray& Indices() { return *reinterpret_cast<MfcArray*>(0x007AEF60); }             // words
std::uint8_t& Guards() { return *reinterpret_cast<std::uint8_t*>(0x007AEFB0); }
AlignedArray& CornersCopy() { return *reinterpret_cast<AlignedArray*>(0x007AEFE8); } // the records point here
AlignedArray& Records() { return *reinterpret_cast<AlignedArray*>(0x007AEFD8); }     // SpriteSortRecord
AlignedArray& Order() { return *reinterpret_cast<AlignedArray*>(0x007AEFC8); }       // record pointers
AlignedArray& Spare() { return *reinterpret_cast<AlignedArray*>(0x007AEFB8); }       // record pointers
std::uint8_t& SortGuards() { return *reinterpret_cast<std::uint8_t*>(0x007AEFF8); }

char* __fastcall Alloc16(int a0) { return AllocAligned16(a0); }
__declspec(naked) void __fastcall ByteArrayConstruct(MfcArray*) EI_ORIGINAL(0x006FF5EA)
__declspec(naked) void __fastcall WordArrayConstruct(MfcArray*) EI_ORIGINAL(0x006FEE9A)
inline void __fastcall ByteArraySetSize(MfcArray* a, void*, int n, int g) { ByteArraySetSizeOriginal(a, nullptr, n, g); }
inline void __fastcall WordArraySetSize(MfcArray* a, void*, int n, int g) { WordArraySetSizeOriginal(a, nullptr, n, g); }
// The aligned arrays' SetSize instantiations (thiscall: size, growth or -1): 32-byte elements keeping
// the contents (00504F70) or not (00505080), 16-byte (00505150) and 4-byte (00505270) elements.
__declspec(naked) void __fastcall SetSize32(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00504F70)
__declspec(naked) void __fastcall SetSize32Discarding(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00505080)
__declspec(naked) void __fastcall SetSize16(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00505150)
__declspec(naked) void __fastcall SetSize4(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00505270)
// Through the hooked addresses (ours in the release build, the verify wrappers in the verify build).
__declspec(naked) int __fastcall ClipTriangle(TLVertex*, const std::uint8_t*, std::uint16_t*, int*, int*, float,
                                              float) EI_ORIGINAL(0x0042D910)
__declspec(naked) void __fastcall RadixPassAt(int, SpriteSortRecord* const*, SpriteSortRecord**, int, int)
    EI_ORIGINAL(0x00503710)
__declspec(naked) void __fastcall MergePassAt(int, SpriteSortRecord* const*, SpriteSortRecord**, int)
    EI_ORIGINAL(0x005037D0)
__declspec(naked) int __fastcall DrawIndexedAt(char*, void*, int, int, void*, int, std::uint16_t*, int, int)
    EI_ORIGINAL(0x00503680)

void(__cdecl* AtExit(std::uintptr_t address))() { return reinterpret_cast<void(__cdecl*)()>(address); }

// First use: the two aligned arrays are zeroed, the MFC arrays constructed; each gets its exit handler.
void EnsureStatics() {
    if (!(Guards() & 1)) {
        Guards() |= 1;
        Unused() = AlignedArray{};
        GameAtExit(AtExit(0x00503A20));
    }
    if (!(Guards() & 2)) {
        Guards() |= 2;
        Corners() = AlignedArray{};
        GameAtExit(AtExit(0x00503A00));
    }
    if (!(Guards() & 4)) {
        Guards() |= 4;
        ByteArrayConstruct(&Outcodes());
        GameAtExit(AtExit(0x005039F0));
    }
    if (!(Guards() & 8)) {
        Guards() |= 8;
        WordArrayConstruct(&Indices());
        GameAtExit(AtExit(0x005039E0));
    }
}

// The sort's statics, constructed (zeroed) on the first batch.
void EnsureSortStatics() {
    struct Static {
        std::uint8_t bit;
        AlignedArray& (*array)();
        std::uintptr_t onExit;
    };
    static const Static kStatics[] = {
        {1, &CornersCopy, 0x005039C0}, {2, &Records, 0x005039A0}, {4, &Order, 0x00503980}, {8, &Spare, 0x00503960},
    };
    for (const Static& s : kStatics) {
        if (SortGuards() & s.bit) continue;
        SortGuards() |= s.bit;
        s.array() = AlignedArray{};
        GameAtExit(AtExit(s.onExit));
    }
}

// The growth step of the aligned arrays: growBy, else size / 8 (rounded towards zero) in 4..1024.
int GrowthStep(const AlignedArray& a) {
    if (a.growBy != 0) return a.growBy;
    int step = a.size / 8;
    if (step < 4) return 4;
    if (step > 0x400) return 0x400;
    return step;
}

// SetSize without keeping the contents, inlined by the original: a new block holds the larger of
// `size` and capacity + growth step.
void Resize(AlignedArray& a, int size, char* (__fastcall* alloc)(int)) {
    if (!a.data) {
        a.data = alloc(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int grown = GrowthStep(a) + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = alloc(capacity);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

TLVertex* CornerData() { return reinterpret_cast<TLVertex*>(Corners().data); }
std::uint16_t* IndexData() { return static_cast<std::uint16_t*>(Indices().data); }

// --- Device calls --------------------------------------------------------------------------------

void* Device(char* renderer) { return At<void*>(At<char*>(renderer, 0x20), 0x54); }
template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}
template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}
constexpr int kSetRenderState = 20;
constexpr int kGetRenderState = 21;
constexpr int kDrawIndexedPrimitive = 26;
constexpr int kSourceBlend = 0x13;
constexpr int kDestBlend = 0x14;
constexpr int kTriangleList = 4;
constexpr int kFvfTlVertex = 0x1C4;

// --- The steps -----------------------------------------------------------------------------------

// A float copied through the FPU (FLD / FSTP dword): exact, except that a signalling NaN comes out
// quiet.
std::uint32_t ThroughFpu(std::uint32_t bits) {
    bool nan = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
    return nan ? bits | 0x00400000u : bits;
}

// The top of the pipeline's matrix stack at +0x24 (data) / +0x28 (count), transposed. The original
// moves the lower triangle up as integers and the upper one down through the FPU.
void TransposedTop(char* pipeline, std::uint32_t (&m)[16]) {
    const char* top = At<char*>(pipeline, 0x24) + (At<int>(pipeline, 0x28) - 1) * 64;
    std::memcpy(m, top, sizeof(m));
    for (int r = 0; r < 4; ++r) {
        for (int c = r + 1; c < 4; ++c) {
            std::uint32_t upper = m[r * 4 + c];
            m[r * 4 + c] = m[c * 4 + r];
            m[c * 4 + r] = ThroughFpu(upper);
        }
    }
}

void SetCorner(TLVertex& v, std::uint32_t color, const float* u, const float* t) {
    v.color = color;
    v.specular = 0xFF000000;
    std::memcpy(&v.u, u, 4);
    std::memcpy(&v.v, t, 4);
}

// The colour and texture coordinates of the next sprites' corners, at most 511 and at most
// `remaining`; corners (u0,v0), (u1,v0), (u1,v1), (u0,v1). Returns how many.
int FillCorners(const Sprite*& sprite, int& remaining) {
    TLVertex* v = CornerData();
    int count = 0;
    while (remaining > 0) {
        const float* uv = sprite->uv;
        std::uint32_t color = sprite->color;
        SetCorner(v[0], color, &uv[0], &uv[1]);
        SetCorner(v[1], color, &uv[2], &uv[1]);
        SetCorner(v[2], color, &uv[2], &uv[3]);
        SetCorner(v[3], color, &uv[0], &uv[3]);
        v += 4;
        ++count;
        --remaining;
        ++sprite;
        if (count >= 0x1FF) break;
    }
    return count;
}

// Sorts the batch back to front: a record per sprite whose first corner's z is not negative (its
// bits are the key), sorted largest first by four radix passes above 128 sprites, else by merge
// passes; the corners and outcodes are then written back in that order. Returns the sprites kept.
int SortBackToFront() {
    EnsureSortStatics();
    SetSize32(&CornersCopy(), nullptr, Corners().size, -1);
    std::memcpy(CornersCopy().data, Corners().data, static_cast<std::size_t>(Corners().size) * sizeof(TLVertex));
    const auto* outcodes = static_cast<const std::uint32_t*>(Outcodes().data);   // four bytes per sprite
    TLVertex* corners = reinterpret_cast<TLVertex*>(CornersCopy().data);
    int sprites = CornersCopy().size / 4;
    SetSize16(&Records(), nullptr, sprites, -1);
    SetSize4(&Order(), nullptr, sprites, -1);
    int kept = 0;
    for (int i = 0; i < sprites; ++i, corners += 4) {
        SpriteSortRecord* record = reinterpret_cast<SpriteSortRecord*>(Records().data) + kept;
        std::memcpy(&record->key, &corners->z, 4);       // written for dropped sprites too
        if (record->key < 0) continue;
        record->corners = corners;
        record->outcodes = outcodes[i];
        reinterpret_cast<SpriteSortRecord**>(Order().data)[kept] = record;
        ++kept;
    }
    SetSize4(&Spare(), nullptr, kept, -1);

    auto* sorted = reinterpret_cast<SpriteSortRecord**>(Order().data);
    auto* other = reinterpret_cast<SpriteSortRecord**>(Spare().data);
    if (kept > 0x80) {
        RadixPassAt(0, sorted, other, kept, 1);
        RadixPassAt(8, other, sorted, kept, 0);
        RadixPassAt(16, sorted, other, kept, 1);
        RadixPassAt(24, other, sorted, kept, 0);
    } else if (kept > 1) {
        int width = 0x40000000;          // the largest power of two below the count, then halving
        if (kept <= width) {
            do width >>= 1;
            while (width >= kept);
        }
        while (width > 0) {
            MergePassAt(width, sorted, other, kept);
            std::swap(sorted, other);
            width >>= 1;
        }
    }

    SetSize32Discarding(&Corners(), nullptr, kept * 4, -1);
    TLVertex* out = CornerData();
    auto* codes = static_cast<std::uint32_t*>(Outcodes().data);
    for (int i = 0; i < kept; ++i) {
        std::memcpy(out + i * 4, sorted[i]->corners, 4 * sizeof(TLVertex));
        codes[i] = sorted[i]->outcodes;
    }
    return Corners().size / 4;
}

// The batch's triangle list through the device holder (renderer + 0x20); with +0x158 set the
// vertices go to the tracker (+0xAC -> +0xC, slot 35) first. False when the draw fails.
bool Draw(char* renderer, int vertexCount, int indexCount) {
    char* holder = At<char*>(renderer, 0x20);
    TLVertex* vertices = CornerData();
    std::uint16_t* indices = IndexData();
    if (At<int>(holder, 0x158)) {
        void* tracker = At<void*>(At<char*>(holder, 0xAC), 0xC);
        ThisCall<void>(tracker, 35, static_cast<void*>(vertices), vertexCount);
    }
    return DrawIndexedAt(holder, nullptr, kTriangleList, kFvfTlVertex, vertices, vertexCount, indices, indexCount, 0) !=
           0;
}

void SetTriangle(std::uint16_t* tri, int a, int b, int c) {
    tri[0] = static_cast<std::uint16_t>(a);
    tri[1] = static_cast<std::uint16_t>(b);
    tri[2] = static_cast<std::uint16_t>(c);
}

// All corners on screen: two triangles per sprite, (0, 2, 1) and (0, 3, 2).
bool DrawQuads(char* renderer, int sprites) {
    std::uint16_t* tri = IndexData();
    for (int i = 0; i < sprites * 4; i += 4, tri += 6) {
        SetTriangle(tri, i, i + 2, i + 1);
        SetTriangle(tri + 3, i, i + 3, i + 2);
    }
    if (sprites == 0) return true;
    return Draw(renderer, sprites * 4, sprites * 6);
}

// The render target's size (the holder's +0x18 object, +0x14 / +0x18) while there is a primary
// surface (+0x14); the original keeps both in stack slots it never initialises, so without a
// surface the previous batch's values (here 0 at first) are used.
struct ScreenSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

float RoundToFloat(double value) {
    volatile float rounded = static_cast<float>(value);
    return rounded;
}

// Some corners off screen: each triangle goes through the clipper, which may append vertices.
bool DrawClipped(char* renderer, int sprites, ScreenSize& screen) {
    char* holder = At<char*>(renderer, 0x20);
    if (At<void*>(holder, 0x14)) {
        char* target = At<char*>(holder, 0x18);
        screen.width = At<std::uint32_t>(target, 0x14);
        screen.height = At<std::uint32_t>(target, 0x18);
    }
    float xMax = RoundToFloat(x87::FromUnsigned(screen.width));
    float yMax = RoundToFloat(x87::FromUnsigned(screen.height));
    TLVertex* vertices = CornerData();
    const auto* outcodes = static_cast<const std::uint8_t*>(Outcodes().data);
    std::uint16_t* tri = IndexData();
    int vertexCount = sprites * 4;
    int indexCount = 0;
    for (int i = 0; i < sprites * 4; i += 4) {
        int added;
        SetTriangle(tri, i, i + 2, i + 1);
        ClipTriangle(vertices, outcodes, tri, &vertexCount, &added, xMax, yMax);
        indexCount += added;
        tri += added;
        SetTriangle(tri, i, i + 3, i + 2);
        ClipTriangle(vertices, outcodes, tri, &vertexCount, &added, xMax, yMax);
        indexCount += added;
        tri += added;
    }
    if (indexCount <= 0) return true;
    return Draw(renderer, vertexCount, indexCount);
}

} // namespace

// --- 00503710 / 005037D0 / 00503680 --------------------------------------------------------------

// 00503710. One radix sort pass: the records of `src` go to 256 buckets by the key's byte at
// `shift` (each bucket a list through the records' link, the last one first), then the buckets go
// to `dst` in ascending order (`ascending` set) or descending. __fastcall: shift in ECX, src in EDX;
// RET 0xC.
void __fastcall SpriteRadixPass(int shift, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count,
                                int ascending) {
    SpriteSortRecord* buckets[256] = {};
    for (int i = 0; i < count; ++i) {
        SpriteSortRecord* record = src[i];
        int bucket = (record->key >> shift) & 0xFF;
        record->next = buckets[bucket];
        buckets[bucket] = record;
    }
    for (int i = 0; i < 256; ++i) {
        for (SpriteSortRecord* r = buckets[ascending ? i : 255 - i]; r; r = r->next) *dst++ = r;
    }
}
EI_REPLACE(0x00503710, SpriteRadixPass);

// 005037D0. One merge sort pass over `count` record pointers: for each column j below `width`, the
// sorted runs src[j], src[j + 2w], ... and src[j + w], src[j + 3w], ... merge, larger key first
// (the first run's on ties), into dst[j], dst[j + w], ...; when 2w > count the last `width`
// pointers are first copied as they are (the columns without a second run). __fastcall: width in
// ECX, src in EDX; RET 8.
void __fastcall SpriteMergePass(int width, SpriteSortRecord* const* src, SpriteSortRecord** dst, int count) {
    int step = width * 2;
    if (step > count) {
        std::memcpy(dst + (count - width), src + (count - width), static_cast<std::size_t>(width) * sizeof(*src));
    }
    int columns = width < count - width ? width : count - width;
    for (int j = 0; j < columns; ++j) {
        int a = j;
        int b = j + width;
        int out = j;
        for (;;) {
            if (src[a]->key >= src[b]->key) {
                dst[out] = src[a];
                a += step;
                out += width;
                if (a >= count) {
                    for (; b < count; b += step, out += width) dst[out] = src[b];
                    break;
                }
            } else {
                dst[out] = src[b];
                b += step;
                out += width;
                if (b >= count) {
                    for (; a < count; a += step, out += width) dst[out] = src[a];
                    break;
                }
            }
        }
    }
}
EI_REPLACE(0x005037D0, SpriteMergePass);

// 00503680 (thiscall on the device holder, RET 0x1C). DrawIndexedPrimitive under the device's
// critical section (007AEE78); the result goes to +8; counts vertices, triangles (indices / 3) and
// calls (+0x16C..+0x174). Returns 1.
int __fastcall DrawIndexed(char* holder, void*, int primitive, int fvf, void* vertices, int vertexCount,
                           std::uint16_t* indices, int indexCount, int flags) {
    auto enter = *reinterpret_cast<void(__stdcall* const*)(void*)>(0x0073B1B4);
    auto leave = *reinterpret_cast<void(__stdcall* const*)(void*)>(0x0073B230);
    void* section = reinterpret_cast<void*>(0x007AEE78);
    // The new renderer (src/render) takes the draw; with the old path's objects off it is skipped.
    if (render::TlDraw(holder, vertices, fvf, vertexCount, indices, indexCount, primitive)) {
        enter(section);
        At<long>(holder, 8) = ComCall(At<void*>(holder, 0x54), kDrawIndexedPrimitive, primitive, fvf, vertices,
                                      vertexCount, indices, indexCount, flags);
        leave(section);
    }
    At<int>(holder, 0x16C) += vertexCount;
    At<std::uint32_t>(holder, 0x170) += static_cast<std::uint32_t>(indexCount) / 3;
    At<int>(holder, 0x174) += 1;
    return 1;
}
EI_REPLACE(0x00503680, DrawIndexed);

// --- 00502CA0 ------------------------------------------------------------------------------------

// 00502CA0 (thiscall on the renderer, RET 0xC). Draws a sprite per point: `points` an aligned array
// of SpritePoint, `sprites` an MFC-style array of Sprite (same count). With `additive` (only its low
// byte counts) the blend is source alpha + one for the call (render states 0x13 / 0x14 saved and
// restored). The view matrix is the top of the pipeline's stack at +0x24, transposed. Returns 1, or
// 0 when a draw fails (the blend is then left as it is).
int __fastcall DrawSprites(char* renderer, void*, char additive, const void* pointArray, const void* spriteArray) {
    const auto* points = static_cast<const PointArray*>(pointArray);
    const auto* sprites = static_cast<const SpriteArray*>(spriteArray);
    char* pipeline = At<char*>(renderer, 8);
    void* transform = At<void*>(pipeline, 0xC);
    EnsureStatics();
    Resize(Unused(), 0x200, &Alloc16);
    Resize(Corners(), 0x2000, &AllocVertices);
    ByteArraySetSize(&Outcodes(), nullptr, 0x800, -1);
    WordArraySetSize(&Indices(), nullptr, 0x3000, -1);
    std::uint32_t view[16];
    TransposedTop(pipeline, view);

    const SpritePoint* point = points->data;
    int remaining = points->size;
    const Sprite* sprite = sprites->data;
    std::uint32_t savedSource = 0;
    std::uint32_t savedDest = 0;
    if (additive) {
        ComCall(Device(renderer), kGetRenderState, kSourceBlend, &savedSource);
        ComCall(Device(renderer), kGetRenderState, kDestBlend, &savedDest);
        ComCall(Device(renderer), kSetRenderState, kSourceBlend, 5);      // D3DBLEND_SRCALPHA
        ComCall(Device(renderer), kSetRenderState, kDestBlend, 2);        // D3DBLEND_ONE
        render::state::Render(kSourceBlend, 5);
        render::state::Render(kDestBlend, 2);
    }

    ScreenSize screen;
    while (remaining > 0) {
        int count = FillCorners(sprite, remaining);
        Resize(Corners(), count * 4, &AllocVertices);
        // 0: all corners on screen, 1: some off screen, else nothing to draw.
        int visibility = ThisCall<int>(transform, 24, static_cast<const void*>(view), static_cast<const void*>(point),
                                       static_cast<void*>(Corners().data), Outcodes().data, count);
        point += count;
        int kept = SortBackToFront();
        if (visibility == 1) {
            if (!DrawClipped(renderer, kept, screen)) return 0;
        } else if (visibility == 0) {
            if (!DrawQuads(renderer, kept)) return 0;
        }
    }

    if (additive) {
        ComCall(Device(renderer), kSetRenderState, kSourceBlend, savedSource);
        ComCall(Device(renderer), kSetRenderState, kDestBlend, savedDest);
        render::state::Render(kSourceBlend, static_cast<std::uint32_t>(savedSource));
        render::state::Render(kDestBlend, static_cast<std::uint32_t>(savedDest));
    }
    return 1;
}
EI_REPLACE(0x00502CA0, DrawSprites);
