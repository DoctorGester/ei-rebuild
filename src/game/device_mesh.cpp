// The Direct3D device's indexed mesh draws: 004E9010 (thiscall, RET 0x48) and 004E9A50 (the same
// with two texture arguments, RET 0x50), on the device object at the renderer's +0x20. The mesh's
// positions (blocks of four vertices) are projected by the pipeline's transform object (+0xAC ->
// +0xC; slot 14, or slot 9 with an outcode per vertex when clipping is asked for), then every
// corner (position, colour and texture coordinate indices) becomes a TL vertex; meshes crossing the
// screen edges go through the transform object's clipper (slot 37) and are projected again (slot
// 34). Figures (005A7A70, 005A7640) and the 3D library's meshes (004F3C60 ...) draw through them.
// Buffers are globals at 007AEDE0..007AEE3C. See docs/notes/models.md ("Renderer").
#include "clip.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct MeshCorner {           // 6 bytes
    std::uint16_t position;   // into the projected vertices
    std::uint16_t colour;     // into the colour and specular arrays
    std::uint16_t uv;         // into the texture coordinates
};
static_assert(sizeof(MeshCorner) == 6);
struct TexCoord {
    float u, v;
};

// The arguments both draws share (004E9A50 has the two textures before them).
struct MeshArgs {
    const void* positions;        // 64 bytes per block of four vertices
    int blocks;
    int unused5;
    int colourCount;              // the colour arrays hold four times this
    std::uint32_t* colours;
    std::uint32_t* speculars;
    const TexCoord* uvs;
    int unused10;
    const MeshCorner* corners;
    int cornerCount;
    int unused13;
    int unused14;
    std::uint16_t* indices;       // into the corners
    int indexCount;
    const float* uOffset;         // the arguments themselves (their bits are compared and added)
    const float* vOffset;
    int alpha;                    // the low byte, with flag 4
    int flags;                    // 1 screen bounds, 2 clip, 4 colours from the device (+0x150) and alpha
};

struct Textures {
    void* first;
    void* second;
};

// --- The buffers ---------------------------------------------------------------------------------

struct AlignedArray {         // the engine's aligned array: data (32-byte aligned), size, capacity, growth
    char* data;
    int size;
    int capacity;
    int growBy;
};

AlignedArray& Vertices() { return *reinterpret_cast<AlignedArray*>(0x007AEE00); }        // TL vertices
AlignedArray& ClipIndices() { return *reinterpret_cast<AlignedArray*>(0x007AEDE0); }     // words
AlignedArray& Projected() { return *reinterpret_cast<AlignedArray*>(0x007AEE10); }       // x, y, z, rhw
AlignedArray& CornerOutcodes() { return *reinterpret_cast<AlignedArray*>(0x007AEE20); }  // a byte per corner
AlignedArray& Outcodes() { return *reinterpret_cast<AlignedArray*>(0x007AEE30); }        // a byte per vertex

__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) char* __fastcall Alloc16(int) EI_ORIGINAL(0x00468F70)
__declspec(naked) char* __fastcall AllocBytes(int) EI_ORIGINAL(0x004ECB50)
__declspec(naked) char* __fastcall AllocVertices(int) EI_ORIGINAL(0x004ECB70)
__declspec(naked) char* __fastcall AllocWords(int) EI_ORIGINAL(0x00406B30)
// SetSize keeping the contents (thiscall: size, growth or -1): TL vertices 004EC790, words 0044FA90.
__declspec(naked) void __fastcall VerticesSetSize(AlignedArray*, void*, int, int) EI_ORIGINAL(0x004EC790)
__declspec(naked) void __fastcall WordsSetSize(AlignedArray*, void*, int, int) EI_ORIGINAL(0x0044FA90)
// The device (thiscall): the screen bounds of projected vertices (004E8900, the array and 0) or of
// clipped TL vertices (004E86B0, the array and their outcodes); a vertex buffer for n vertices
// (004E7820, always null) and its draw (004E7E00: indices, count); the draws with one (004EB550) or
// two textures (004EB370): primitive, FVF, vertices, count, indices, count, flags.
__declspec(naked) void __fastcall BoundsOfProjected(char*, void*, AlignedArray*, int) EI_ORIGINAL(0x004E8900)
__declspec(naked) void __fastcall BoundsOfVertices(char*, void*, AlignedArray*, char*) EI_ORIGINAL(0x004E86B0)
__declspec(naked) void* __fastcall LockVertexBuffer(char*, void*, int) EI_ORIGINAL(0x004E7820)
__declspec(naked) void __fastcall DrawVertexBuffer(char*, void*, std::uint16_t*, int) EI_ORIGINAL(0x004E7E00)
__declspec(naked) int __fastcall DrawPrimitive(char*, void*, int, int, void*, int, std::uint16_t*, int, int)
    EI_ORIGINAL(0x004EB550)
__declspec(naked) int __fastcall DrawPrimitive2(char*, void*, void*, void*, int, int, void*, int, std::uint16_t*, int,
                                                int) EI_ORIGINAL(0x004EB370)

int GrowthStep(const AlignedArray& a) {
    if (a.growBy != 0) return a.growBy;
    int step = a.size / 8;
    if (step < 4) return 4;
    if (step > 0x400) return 0x400;
    return step;
}

// SetSize without keeping the contents (a new block holds the larger of `size` and capacity +
// growth step).
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

// SetSize keeping the contents; 0 frees the block.
void SetSize(AlignedArray& a, int size, int elementSize, char* (__fastcall* alloc)(int)) {
    if (size == 0) {
        if (a.data) {
            GameDelete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
        return;
    }
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
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * elementSize);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

TLVertex* VertexData() { return reinterpret_cast<TLVertex*>(Vertices().data); }

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// The top matrix of the pipeline's stack at `offset` (data; the count follows).
const void* StackTop(char* device, int offset) {
    char* pipeline = At<char*>(device, 0xAC);
    return At<char*>(pipeline, offset) + (At<int>(pipeline, offset + 4) - 1) * 64;
}

// FCOMP with 0.0 (0073B858) then C3: equal, or unordered.
bool ZeroOrNaN(const float* value) {
    std::uint32_t bits;
    std::memcpy(&bits, value, 4);
    return (bits & 0x7FFFFFFFu) == 0 || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// *out = *offset + *value: FLD the offset, FADD the coordinate, FSTP (the operand order decides
// which NaN comes out when both are NaNs).
__declspec(naked) void __fastcall AddOffset(const float* /*offset*/, const float* /*value*/, float* /*out*/) {
    __asm {
        fld dword ptr [ecx]
        fadd dword ptr [edx]
        mov eax, dword ptr [esp + 4]
        fstp dword ptr [eax]
        ret 4
    }
}

// --- The steps -----------------------------------------------------------------------------------

// Flag 4: every colour becomes the device's (+0x150), every specular the alpha byte alone.
void OverrideColours(char* device, const MeshArgs& a) {
    std::uint32_t colour = At<std::uint32_t>(device, 0x150);
    std::uint32_t specular = (static_cast<std::uint32_t>(a.alpha) & 0xFF) << 24;
    int count = a.colourCount * 4;
    for (int i = 0; i < count; ++i) a.colours[i] = colour;
    for (int i = 0; i < count; ++i) a.speculars[i] = specular;
}

// The corners as TL vertices: the projected position (copied as two qwords), colour and specular,
// and the texture coordinate, moved by the offsets unless both compare equal to 0.
void FillCorners(const MeshArgs& a) {
    TLVertex* v = VertexData();
    const char* projected = Projected().data;
    bool moved = !(ZeroOrNaN(a.uOffset) && ZeroOrNaN(a.vOffset));
    for (int i = 0; i < a.cornerCount; ++i, ++v) {
        const MeshCorner& c = a.corners[i];
        std::memcpy(&v->x, projected + c.position * 16, 16);
        v->color = a.colours[c.colour];
        v->specular = a.speculars[c.colour];
        if (moved) {
            AddOffset(a.uOffset, &a.uvs[c.uv].u, &v->u);
            AddOffset(a.vOffset, &a.uvs[c.uv].v, &v->v);
        } else {
            std::memcpy(&v->u, &a.uvs[c.uv], 8);
        }
    }
}

// Hands the vertices to a vertex buffer (never available: 004E7820 returns null) or draws them.
void Submit(char* device, const Textures* textures, std::uint16_t* indices, int indexCount) {
    int count = Vertices().size;
    if (void* buffer = LockVertexBuffer(device, nullptr, count)) {
        std::memcpy(buffer, Vertices().data, static_cast<std::size_t>(count) * sizeof(TLVertex));
        DrawVertexBuffer(device, nullptr, indices, indexCount);
        At<int>(device, 0xE0) += count;
        return;
    }
    if (textures) {
        DrawPrimitive2(device, nullptr, textures->first, textures->second, 4, 0x1C4, Vertices().data, count, indices,
                       indexCount, 0);
    } else {
        DrawPrimitive(device, nullptr, 4, 0x1C4, Vertices().data, count, indices, indexCount, 0);
    }
}

void DrawOnScreen(char* device, void* transform, const MeshArgs& a, const Textures* textures) {
    if (a.flags & 1) BoundsOfProjected(device, nullptr, &Projected(), 0);
    Resize(Vertices(), a.cornerCount, &AllocVertices);
    FillCorners(a);
    if (At<int>(device, 0x158)) ThisCall<void>(transform, 35, static_cast<void*>(Vertices().data), a.cornerCount);
    Submit(device, textures, a.indices, a.indexCount);
}

// Some vertices off screen: room for ten times the corners and indices, the corners' outcodes, the
// transform object's clipper (slot 37) writes the clipped triangles to ClipIndices, then the
// vertices are projected (slot 34).
void DrawClipped(char* device, void* transform, const MeshArgs& a, const Textures* textures) {
    int capacity = a.cornerCount * 10;
    Resize(Vertices(), capacity, &AllocVertices);
    SetSize(ClipIndices(), a.indexCount * 10, 2, &AllocWords);
    FillCorners(a);
    SetSize(CornerOutcodes(), capacity, 1, &AllocBytes);
    for (int i = 0; i < a.cornerCount; ++i) CornerOutcodes().data[i] = Outcodes().data[a.corners[i].position];

    int vertexCount = a.cornerCount;
    int indexCount = a.indexCount;
    ThisCall<int>(transform, 37, static_cast<void*>(Vertices().data), static_cast<void*>(CornerOutcodes().data),
                  a.indices, static_cast<void*>(ClipIndices().data), &vertexCount, &indexCount);
    if (indexCount == 0 || vertexCount == 0) return;
    VerticesSetSize(&Vertices(), nullptr, vertexCount, -1);
    WordsSetSize(&ClipIndices(), nullptr, indexCount, -1);
    ThisCall<void>(transform, 34, static_cast<void*>(Vertices().data), Vertices().size);
    if (At<int>(device, 0x158)) ThisCall<void>(transform, 35, static_cast<void*>(Vertices().data), Vertices().size);
    if (a.flags & 1) BoundsOfVertices(device, nullptr, &Vertices(), CornerOutcodes().data);
    Submit(device, textures, reinterpret_cast<std::uint16_t*>(ClipIndices().data), ClipIndices().size);
}

// Returns the transform's verdict: 2 nothing on screen, 1 clipped, 0 on screen (drawn: 1).
int DrawMesh(char* device, const MeshArgs& a, const Textures* textures) {
    void* transform = At<void*>(At<char*>(device, 0xAC), 0xC);
    Resize(Projected(), a.blocks * 4, &Alloc16);
    int visibility;
    if (a.flags & 2) {
        Resize(Outcodes(), a.blocks * 4, &AllocBytes);
        visibility = ThisCall<int>(transform, 9, StackTop(device, 0x24), a.positions,
                                   static_cast<void*>(Projected().data), static_cast<void*>(Outcodes().data), a.blocks);
    } else {
        visibility = ThisCall<int>(transform, 14, StackTop(device, 0x1C), a.positions,
                                   static_cast<void*>(Projected().data), a.blocks);
    }
    if (visibility == 2) return 2;
    if (a.flags & 4) OverrideColours(device, a);
    if (visibility == 0) {
        DrawOnScreen(device, transform, a, textures);
        return 1;
    }
    if (visibility == 1) DrawClipped(device, transform, a, textures);
    return visibility;
}

} // namespace

// 004E9010 (thiscall on the device, RET 0x48).
int __fastcall DeviceDrawMesh(char* device, void*, const void* positions, int blocks, int unused5, int colourCount,
                              std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
                              const void* corners, int cornerCount, int unused13, int unused14,
                              std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha,
                              int flags) {
    MeshArgs a{positions, blocks, unused5, colourCount, colours, speculars, static_cast<const TexCoord*>(uvs),
               unused10, static_cast<const MeshCorner*>(corners), cornerCount, unused13, unused14, indices,
               indexCount, &uOffset, &vOffset, alpha, flags};
    return DrawMesh(device, a, nullptr);
}
EI_REPLACE(0x004E9010, DeviceDrawMesh);

// 004E9A50 (thiscall on the device, RET 0x50): the same with two textures for the draw (004EB370).
int __fastcall DeviceDrawMesh2(char* device, void*, void* firstTexture, void* secondTexture, const void* positions,
                               int blocks, int unused5, int colourCount, std::uint32_t* colours,
                               std::uint32_t* speculars, const void* uvs, int unused10, const void* corners,
                               int cornerCount, int unused13, int unused14, std::uint16_t* indices, int indexCount,
                               float uOffset, float vOffset, int alpha, int flags) {
    MeshArgs a{positions, blocks, unused5, colourCount, colours, speculars, static_cast<const TexCoord*>(uvs),
               unused10, static_cast<const MeshCorner*>(corners), cornerCount, unused13, unused14, indices,
               indexCount, &uOffset, &vOffset, alpha, flags};
    Textures textures{firstTexture, secondTexture};
    return DrawMesh(device, a, &textures);
}
EI_REPLACE(0x004E9A50, DeviceDrawMesh2);
