// Checks for the sprite shape builders (ui_mesh_build.cpp).
//
// Side by side on the same sprite: the original builds first and what it wrote is copied (the
// quads, vertex records, indices, texture coordinates, the +0x1D0 block, the sizes of the +0x138 /
// +0x148 arrays, position, scale and the renderer fields Commit sets); then all of that is
// overwritten with 0xCD bytes (the arrays keep their storage: ours sizes them the same way and does
// not reallocate), ours builds, and the two copies must be equal byte for byte.
#include "verify.h"

#include "../../src/game/ui_mesh_build.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct ArrayView {
    std::uint8_t* data;
    int size;
};
ArrayView ArrayAt(CUiMesh* mesh, int offset) {
    auto* p = reinterpret_cast<std::uint8_t*>(mesh) + offset;
    ArrayView v;
    std::memcpy(&v.data, p, 4);
    std::memcpy(&v.size, p + 4, 4);
    return v;
}

struct MeshState {
    std::vector<std::uint8_t> bytes;
    int sizes[7] = {};

    // The parts in a fixed order; `poison` overwrites them instead of reading.
    void Visit(CUiMesh* mesh, bool poison) {
        auto* m = reinterpret_cast<std::uint8_t*>(mesh);
        struct Part {
            int offset;     // array header, or a field run
            int element;    // element size (0: a plain field run of `count` bytes)
            int count;
        };
        const Part parts[] = {
            {0x1C0, 0x40, 0}, {0x1D0, 0x40, 0}, {0x1E0, 6, 0}, {0x200, 2, 0}, {0x210, 8, 0},
            {0x1C, 0, 12},    {0x23C, 0, 4},    {0x250, 0, 4}, {0x264, 0, 4}, {0x9C, 0, 0xCC - 0x9C},
        };
        int sizeIndex = 0;
        for (const Part& part : parts) {
            std::uint8_t* from;
            int length;
            if (part.element) {
                ArrayView a = ArrayAt(mesh, part.offset);
                if (!poison) sizes[sizeIndex] = a.size;
                ++sizeIndex;
                from = a.data;
                length = a.data ? a.size * part.element : 0;
            } else {
                from = m + part.offset;
                length = part.count;
            }
            if (length <= 0) continue;
            if (poison) {
                std::memset(from, 0xCD, length);
            } else {
                bytes.insert(bytes.end(), from, from + length);
            }
        }
        if (!poison) {
            sizes[5] = ArrayAt(mesh, 0x138).size;
            sizes[6] = ArrayAt(mesh, 0x148).size;
        }
    }
    // Commit copies the index array's data pointer to +0xC4. The builders empty the index array
    // with SetSize(0), which frees its storage, so each run gets a new buffer: the copy records
    // whether +0xC4 points at the current indices instead of the address itself.
    static MeshState Read(CUiMesh* mesh) {
        MeshState s;
        s.Visit(mesh, false);
        auto* m = reinterpret_cast<std::uint8_t*>(mesh);
        std::uint32_t pointer, indices;
        std::memcpy(&pointer, m + 0xC4, 4);
        std::memcpy(&indices, m + 0x200, 4);
        std::uint32_t flag = pointer == indices ? 1u : 2u;
        std::size_t at = s.bytes.size() - (0xCC - 0xC4);   // the render fields come last
        std::memcpy(&s.bytes[at], &flag, 4);
        return s;
    }
    bool operator==(const MeshState& o) const {
        return bytes == o.bytes && std::memcmp(sizes, o.sizes, sizeof(sizes)) == 0;
    }
    int FirstDifference(const MeshState& o) const {
        if (bytes.size() != o.bytes.size()) return -2;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (bytes[i] != o.bytes[i]) return static_cast<int>(i);
        return -1;
    }
};

template <typename Original, typename Ours>
void Compare(const char* name, CUiMesh* mesh, Original original, Ours ours) {
    original();
    MeshState expected = MeshState::Read(mesh);
    MeshState().Visit(mesh, true);
    ours();
    MeshState actual = MeshState::Read(mesh);
    ei::verify::Check(name, true, expected == actual, "first difference at byte %d (sizes q %d/%d v %d/%d i %d/%d uv %d/%d)",
                      expected.FirstDifference(actual), expected.sizes[0], actual.sizes[0], expected.sizes[2],
                      actual.sizes[2], expected.sizes[3], actual.sizes[3], expected.sizes[4], actual.sizes[4]);
}

} // namespace

// 005E8450: SUB ESP,0x20; PUSH EBX; MOV EBX,ECX (6).
static void(__thiscall* s_framed)(CUiMeshBuilder*, float, float, float, float, float, float, float, int, int, int, int);
static void __fastcall FramedVerify(CUiMeshBuilder* self, void*, float depth, float scale, float left, float top,
                                    float right, float bottom, float border, int u0, int v0, int u1, int v1) {
    Compare("CUiMeshBuilder::Framed", self,
            [&] { s_framed(self, depth, scale, left, top, right, bottom, border, u0, v0, u1, v1); },
            [&] { self->Framed(depth, scale, left, top, right, bottom, border, u0, v0, u1, v1); });
}
EI_VERIFY(0x005E8450, FramedVerify, 6, s_framed);

// 005E8C60: SUB ESP,8; PUSH EBX; MOV EBX,ECX (6). Place (Rebirth-patched) runs in both builds and
// writes the same position.
static void(__thiscall* s_disc)(CUiMeshBuilder*, float, float, float, float, float, int, int, int, int);
static void __fastcall DiscVerify(CUiMeshBuilder* self, void*, float depth, float scale, float x, float y, float radius,
                                  int count, int u, int v, int uvRadius) {
    Compare("CUiMeshBuilder::Disc", self, [&] { s_disc(self, depth, scale, x, y, radius, count, u, v, uvRadius); },
            [&] { self->Disc(depth, scale, x, y, radius, count, u, v, uvRadius); });
}
EI_VERIFY(0x005E8C60, DiscVerify, 6, s_disc);

// 005E9F30: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (6). Side by side: the block and the
// three arrays' sizes (the block is poisoned between the runs).
static void(__fastcall* s_resetColours)(CUiMeshBuilder*);
static void __fastcall ResetColoursVerify(CUiMeshBuilder* self) {
    s_resetColours(self);
    std::uint8_t expected[0x40];
    ArrayView block = ArrayAt(self, 0x1D0);
    std::memcpy(expected, block.data, sizeof(expected));
    int sizes[3] = {block.size, ArrayAt(self, 0x138).size, ArrayAt(self, 0x148).size};
    std::memset(block.data, 0xCD, sizeof(expected));
    self->ResetColours();
    block = ArrayAt(self, 0x1D0);
    bool ok = std::memcmp(expected, block.data, sizeof(expected)) == 0 && sizes[0] == block.size &&
              sizes[1] == ArrayAt(self, 0x138).size && sizes[2] == ArrayAt(self, 0x148).size;
    ei::verify::Check("CUiMeshBuilder::ResetColours", true, ok, "sizes %d %d %d", sizes[0], sizes[1], sizes[2]);
}
EI_VERIFY(0x005E9F30, ResetColoursVerify, 6, s_resetColours);

// 005E91F0: SUB ESP,0x14; PUSH EBX; MOV EBX,ECX (6).
static void(__thiscall* s_ring)(CUiMeshBuilder*, float, float, float, float, float, int, float, int, int, int, int);
static void __fastcall RingVerify(CUiMeshBuilder* self, void*, float depth, float scale, float x, float y, float radius,
                                  int count, float width, int u0, int v0, int u1, int v1) {
    Compare("CUiMeshBuilder::Ring", self,
            [&] { s_ring(self, depth, scale, x, y, radius, count, width, u0, v0, u1, v1); },
            [&] { self->Ring(depth, scale, x, y, radius, count, width, u0, v0, u1, v1); });
}
EI_VERIFY(0x005E91F0, RingVerify, 6, s_ring);
