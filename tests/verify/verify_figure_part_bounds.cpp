// Checks for the figure part's bounds draw (figure_part_bounds.cpp). Differential through a
// stand-in renderer whose slot 15 records the line list: the original, then ours, each into the
// stand-in; the points and colour compared; then ours once more into the real renderer.
#include "verify.h"

#include "../../src/game/link_figure.h"

#include <cstdint>
#include <cstring>
#include <vector>


namespace {
using BoundsFn = void(__fastcall*)(FigureMeshDrawer*, void*, CSceneRenderer*);
BoundsFn s_original;

struct Captured {
    bool called = false;
    int count = 0;
    std::uint32_t colour = 0;
    std::vector<std::uint32_t> points;   // x, y, z, w per point
};
Captured* s_capture;

void __fastcall CaptureLines(void*, void*, const std::uint8_t* lines) {
    Captured& c = *s_capture;
    c.called = true;
    std::memcpy(&c.count, lines + 0x14, 4);
    std::memcpy(&c.colour, lines + 0x18, 4);
    const std::uint8_t* data;
    std::memcpy(&data, lines + 4, 4);
    for (int i = 0; i < c.count; ++i)
        for (int axis = 0; axis < 4; ++axis) {
            std::uint32_t v;
            std::memcpy(&v, data + ((i >> 2) * 16 + axis * 4 + (i & 3)) * 4, 4);
            c.points.push_back(v);
        }
}

struct StandIn {
    void** vtable;
};
void* s_vtable[16];

// Ours, through the registered member.
void __fastcall BoundsOurs(FigureMeshDrawer* node, void*, CSceneRenderer* renderer) { node->DrawBounds(renderer); }

void RunInto(BoundsFn fn, FigureMeshDrawer* node, Captured& out) {
    for (void*& slot : s_vtable) slot = nullptr;
    s_vtable[15] = reinterpret_cast<void*>(&CaptureLines);
    StandIn standIn{s_vtable};
    s_capture = &out;
    fn(node, nullptr, reinterpret_cast<CSceneRenderer*>(&standIn));
}
} // namespace

// 005A7290.
static void __fastcall DrawPartBoundsVerify(FigureMeshDrawer* node, void*, CSceneRenderer* renderer) {
    Captured expected, actual;
    RunInto(s_original, node, expected);
    RunInto(&BoundsOurs, node, actual);
    bool ok = expected.called == actual.called && expected.count == actual.count && expected.colour == actual.colour &&
              expected.points == actual.points;
    ei::verify::Check("DrawPartBounds", true, ok, "drawn %d/%d, %d points (original %d)", actual.called,
                      expected.called, actual.count, expected.count);
    node->DrawBounds(renderer);
}
EI_VERIFY(0x005A7290, DrawPartBoundsVerify, 7, s_original);

// 005A75D0 (A-W14): the line batch's destructor: ours only (it frees the points' block): the batch
// is left with -1 and an empty array. No scenario reaches it: only the unwind funclet of the
// original DrawPartBounds calls it.
void __fastcall LineBatchDestruct(void* batch);

namespace {
void* s_unused;
void __fastcall LineBatchDestructVerify(void* batch) {
    LineBatchDestruct(batch);
    int fields[4];
    std::memcpy(fields, batch, sizeof(fields));
    ei::verify::Check("LineBatchDestruct", true, fields[0] == -1 && fields[1] == 0 && fields[2] == 0 && fields[3] == 0, "%p", batch);
}
} // namespace
EI_VERIFY(0x005A75D0, LineBatchDestructVerify, 0, s_unused);
