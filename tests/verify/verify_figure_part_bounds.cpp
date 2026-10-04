// Checks for the figure part's bounds draw (figure_part_bounds.cpp). Differential through a
// stand-in renderer whose slot 15 records the line list: the original, then ours, each into the
// stand-in; the points and colour compared; then ours once more into the real renderer.
#include "verify.h"

#include <cstdint>
#include <cstring>
#include <vector>

void __fastcall DrawPartBounds(char* node, void*, void* renderer);

namespace {
using BoundsFn = void(__fastcall*)(char*, void*, void*);
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

void RunInto(BoundsFn fn, char* node, Captured& out) {
    for (void*& slot : s_vtable) slot = nullptr;
    s_vtable[15] = reinterpret_cast<void*>(&CaptureLines);
    StandIn standIn{s_vtable};
    s_capture = &out;
    fn(node, nullptr, &standIn);
}
} // namespace

// 005A7290.
static void __fastcall DrawPartBoundsVerify(char* node, void*, void* renderer) {
    Captured expected, actual;
    RunInto(s_original, node, expected);
    RunInto(&DrawPartBounds, node, actual);
    bool ok = expected.called == actual.called && expected.count == actual.count && expected.colour == actual.colour &&
              expected.points == actual.points;
    ei::verify::Check("DrawPartBounds", true, ok, "drawn %d/%d, %d points (original %d)", actual.called,
                      expected.called, actual.count, expected.count);
    DrawPartBounds(node, nullptr, renderer);
}
EI_VERIFY(0x005A7290, DrawPartBoundsVerify, 7, s_original);
