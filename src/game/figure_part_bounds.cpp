// 005A7290 (thiscall on a figure part's scene node, RET 4): the finishing call of the part draws
// (005A7640, 005A7A70). With the debug flag 0x4000 of the owning manager's context (+8) set, the
// mesh's clip box is drawn as lines through the renderer (slot 15): the corners are the box origin
// (mesh +0x18C) plus, per axis, either the near (+0x19C..) or the far (+0x1A8..) extent; eight
// edges, two points each, in the original's order. See docs/notes/models.md ("Figure parts").
#include "ei/hook.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// The renderer's line list (0x20 bytes): positions in 64-byte blocks of four points (x4, y4, z4,
// w4), the point count at +0x14, the colour at +0x18.
struct LineList {
    int unknown_00;           // -1
    char* data;
    int size;
    int capacity;
    int growBy;
    int count;
    std::uint32_t colour;     // 0xFFFF0000
    int unknown_1C;
};
static_assert(sizeof(LineList) == 0x20);

// 00455940 the owning manager's context (object.cpp). 0049B140 (RET 4) appends a point.
__declspec(naked) char* __fastcall ManagerContextAt(void*) EI_ORIGINAL(0x00455940)
__declspec(naked) void __fastcall AddPoint(LineList*, void*, const float*) EI_ORIGINAL(0x0049B140)
__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)

constexpr std::uint32_t kShowBounds = 0x4000;
constexpr int kSlotDrawLines = 15;

} // namespace

#pragma float_control(precise, on, push)

void __fastcall DrawPartBounds(char* node, void*, void* renderer) {
    if (!(At<std::uint32_t>(ManagerContextAt(node), 8) & kShowBounds)) return;
    LineList lines{-1, nullptr, 0, 0, 0, 0, 0xFFFF0000u, 0};
    char* mesh = At<char*>(node, 0x1C);
    const float* origin = &At<float>(mesh, 0x18C);
    const float* nearExtent = &At<float>(mesh, 0x19C);
    const float* farExtent = &At<float>(mesh, 0x1A8);
    // corner[i]: bit 2 near x, bit 1 near y, bit 0 near z (else far).
    float corner[8][3];
    for (int i = 0; i < 8; ++i) {
        corner[i][0] = origin[0] + ((i & 4) ? nearExtent[0] : farExtent[0]);
        corner[i][1] = origin[1] + ((i & 2) ? nearExtent[1] : farExtent[1]);
        corner[i][2] = origin[2] + ((i & 1) ? nearExtent[2] : farExtent[2]);
    }
    static const int kOrder[16] = {3, 1, 0, 2, 6, 4, 0, 1, 5, 4, 5, 7, 6, 7, 3, 2};
    for (int index : kOrder) AddPoint(&lines, nullptr, corner[index]);
    using DrawLinesFn = void(__fastcall*)(void*, void*, LineList*);
    reinterpret_cast<DrawLinesFn>((*static_cast<void***>(renderer))[kSlotDrawLines])(renderer, nullptr, &lines);
    if (lines.data) GameDelete(reinterpret_cast<void**>(lines.data)[-1]);
}

#pragma float_control(pop)

EI_REPLACE(0x005A7290, DrawPartBounds);
