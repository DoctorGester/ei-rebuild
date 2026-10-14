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
#include "figure_mesh.h"          // PositionBlock
#include "lib/containers.h"
#include "lib/gameapi.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"
#include "texture.h"           // CTextureDevice

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// The figure mesh's colour arrays (+0x138 / +0x148).
using ColourArray = lib::AlignedArray<std::uint32_t>;

// The moved positions (lib::AlignedArray; its inlined SetSize allocates through 00452E70).
lib::AlignedArray<PositionBlock>& MovedPositions() { return *reinterpret_cast<lib::AlignedArray<PositionBlock>*>(0x007C2E60); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E70); }

// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
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

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

// The positions with the per-vertex offsets added, in 007C2E60.
const void* MovedPositionsOf(CFigureMesh* mesh) {
    lib::AlignedArray<PositionBlock>& moved = MovedPositions();
    int blocks = mesh->buffers_.vertexCount;
    moved.SetSize(blocks);
    if (blocks) std::memcpy(moved.data, mesh->buffers_.vertices, static_cast<std::size_t>(blocks) * 64);
    for (int i = 0; i < mesh->offsets_.size; ++i) {
        const float* offset = reinterpret_cast<const float*>(mesh->offsets_.data) + i * 3;
        float* x = moved.data[i >> 2].v + (i & 3);
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
    auto* textureDevice = reinterpret_cast<CTextureDevice*>(device);
    auto mesh = [node] { return At<CFigureMesh*>(node, 0x1C); };
    if (mesh()->flags_ & 0x80) return 1;
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = lib::AlignedArray<PositionBlock>{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A7A50));
    }
    int drawFlags = 0;
    mesh()->flags_ &= ~0x40u;
    int visibility = ClipTest(textureDevice->pipeline_, nullptr, &mesh()->bounds_);
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

    std::uint32_t meshFlags = mesh()->flags_;
    if (meshFlags & 0x100) {
        Fill(mesh()->colorsA_, mesh()->flatColour_);
        Fill(mesh()->colorsB_, mesh()->flatSpecular_);
    } else if (!(meshFlags & 0x10)) {
        drawFlags = 4;
    }
    if (visibility == 1) drawFlags |= 2;
    if (rect) drawFlags |= 1;

    const void* positions = mesh()->buffers_.vertices;
    int blocks = mesh()->buffers_.vertexCount;
    if (mesh()->offsets_.size != 0) positions = MovedPositionsOf(mesh());
    CFigureMesh* m = mesh();
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
            gameapi::CopyRect(own, &textureDevice->drawnRect_);
            *rect = *own;
        }
    }
    if (result != 2) mesh()->flags_ |= 0x40;
    FinishDraw(node, nullptr, reinterpret_cast<void*>(0x007AF060));
    return 1;
}
EI_REPLACE(0x005A7640, DrawFigurePartPlain);
