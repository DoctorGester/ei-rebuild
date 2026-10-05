// The plain draw of one figure part (005A7640, thiscall on the part's scene node, RET 0x18): the
// simpler sibling of 005A7A70 (figure_part_draw.cpp), always through the device and without the
// lighting: a hidden mesh (flag 0x80) is skipped, the drawn flag (0x40) cleared, the mesh
// clip-tested against the view, animated meshes (type 13..16) advanced to the current time; flat
// meshes (0x100) get their colours (+0x138) filled with +0xD4 and speculars (+0x148) with +0xD8
// (the arrays keep their sizes), a mesh not yet lit (no 0x10) asks the device to light it (draw
// flag 4); the per-vertex offsets (+0x128, +0x12C of them) are added into a copy of the positions
// (its own function-local static 007C2E60, guard 007C2E70 bit 1, freed at exit by 005A7A50). Any
// draw that is not off screen marks the mesh drawn. See docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct AlignedArray {         // the engine's aligned array: data (32-byte aligned), size, capacity, growth
    char* data;
    int size;
    int capacity;
    int growBy;
};

AlignedArray& MovedPositions() { return *reinterpret_cast<AlignedArray*>(0x007C2E60); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E70); }

__declspec(naked) char* __fastcall Alloc64(int) EI_ORIGINAL(0x00452E70)
// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
__declspec(naked) int __fastcall ClipTest(void* /*pipeline*/, void*, const void* /*bounds*/) EI_ORIGINAL(0x004F16A0)
// 005A7160: the node's animation at a time (float bits). 005A7290: after the draw (the renderer).
// 004E9010: the device's mesh draw (device_mesh.cpp).

// *target = *offset + *target (FLD offset, FADD, FSTP). __fastcall.
__declspec(naked) void __fastcall AddOffset(const float* /*offset*/, float* /*target*/) {
    __asm {
        fld dword ptr [ecx]
        fadd dword ptr [edx]
        fstp dword ptr [edx]
        ret
    }
}

// The world time: whole ticks (007AF61C, FILD) plus the fraction of the current one (007AFCA8),
// rounded to a float. __fastcall: out in ECX.
__declspec(naked) void __fastcall TickTime(float* /*out*/) {
    __asm {
        fild dword ptr ds:[007AF61Ch]
        fadd dword ptr ds:[007AFCA8h]
        fstp dword ptr [ecx]
        ret
    }
}

// SetSize keeping the contents (64-byte blocks); 0 frees the block. The growth is the array's
// own, else an eighth of the size clamped to [4, 0x400].
void SetSize64(AlignedArray& a, int size) {
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
        a.data = Alloc64(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int step = a.growBy;
    if (step == 0) {
        step = a.size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int grown = step + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = Alloc64(capacity);
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 64);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

void Fill(const AlignedArray& a, std::uint32_t value) {
    auto* p = reinterpret_cast<std::uint32_t*>(a.data);
    for (int i = 0; i < a.size; ++i) p[i] = value;
}

// The positions with the per-vertex offsets added, in 007C2E60.
const void* MovedPositionsOf(char* mesh) {
    AlignedArray& moved = MovedPositions();
    int blocks = At<int>(mesh, 0xA0);
    SetSize64(moved, blocks);
    if (blocks) std::memcpy(moved.data, At<void*>(mesh, 0x9C), static_cast<std::size_t>(blocks) * 64);
    for (int i = 0; i < At<int>(mesh, 0x12C); ++i) {
        const float* offset = reinterpret_cast<const float*>(At<char*>(mesh, 0x128)) + i * 3;
        float* x = reinterpret_cast<float*>(moved.data) + (i >> 2) * 16 + (i & 3);
        AddOffset(&offset[0], &x[0]);
        AddOffset(&offset[1], &x[4]);
        AddOffset(&offset[2], &x[8]);
    }
    return moved.data;
}

} // namespace

// 005A7640 (thiscall on the part's node, RET 0x18). Arguments 2-4 are not read; `alpha` goes to
// the device draw, `rect` (optional) receives the screen rectangle (empty when off screen).
// Returns 1.
int __fastcall DrawFigurePartPlain(char* node, void*, char* device, int, int, int, int alpha, RECT* rect) {
    auto mesh = [node] { return At<char*>(node, 0x1C); };
    if (At<std::uint32_t>(mesh(), 8) & 0x80) return 1;
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = AlignedArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A7A50));
    }
    int drawFlags = 0;
    At<std::uint32_t>(mesh(), 8) &= ~0x40u;
    int visibility = ClipTest(At<void*>(device, 0xAC), nullptr, mesh() + 0x188);
    if (visibility == 2) return 1;
    int type = At<int>(mesh(), 0x15C);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        At<std::uint32_t>(node, 8) |= *reinterpret_cast<const std::uint32_t*>(0x00790C0C);
        std::uint32_t bits;
        std::memcpy(&bits, &time, 4);
        AnimateOriginal(node, nullptr, bits);
    }

    std::uint32_t meshFlags = At<std::uint32_t>(mesh(), 8);
    if (meshFlags & 0x100) {
        Fill(At<AlignedArray>(mesh(), 0x138), At<std::uint32_t>(mesh(), 0xD4));
        Fill(At<AlignedArray>(mesh(), 0x148), At<std::uint32_t>(mesh(), 0xD8));
    } else if (!(meshFlags & 0x10)) {
        drawFlags = 4;
    }
    if (visibility == 1) drawFlags |= 2;
    if (rect) drawFlags |= 1;

    const void* positions = At<void*>(mesh(), 0x9C);
    int blocks = At<int>(mesh(), 0xA0);
    if (At<int>(mesh(), 0x12C) != 0) positions = MovedPositionsOf(mesh());
    char* m = mesh();
    auto field = [m](int offset) { return At<std::uint32_t>(m, offset); };
    int result = DrawMesh(device, nullptr, positions, blocks, field(0xA4), static_cast<int>(field(0xA8)), field(0x138),
                          field(0x148), field(0xAC), field(0xB0), field(0xB4), field(0xB8), field(0xBC), field(0xC0),
                          field(0xC4), field(0xC8), field(0xCC), field(0xD0), alpha, drawFlags);
    if (rect) {
        RECT* own = &At<RECT>(node, 0x20);
        if (result == 2) {
            gameapi::SetRectEmpty(own);
            gameapi::SetRectEmpty(rect);
        } else {
            gameapi::CopyRect(own, &At<RECT>(device, 0x15C));
            *rect = *own;
        }
    }
    if (result != 2) At<std::uint32_t>(mesh(), 8) |= 0x40;
    FinishDraw(node, nullptr, reinterpret_cast<void*>(0x007AF060));
    return 1;
}
EI_REPLACE(0x005A7640, DrawFigurePartPlain);
