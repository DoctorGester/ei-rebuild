// Checks for the cursor set-up (src/game/cursor_setup.cpp). Both create the cursor's thread and
// images, so ours only: afterwards the cursor (007C7BD8) holds at least the 25 shapes.
#include "verify.h"

#include <cstdint>

void __fastcall SetUpCursors(bool modeTwo, int context, int smallCursors, int flags);
void __fastcall ResetCursors(bool modeTwo, int context, int smallCursors, int flags);

namespace {
void* s_unused;
// The cursor's shape array (+0x68 data, +0x6C count).
struct CursorShapes {
    std::uint8_t unused_00[0x68];
    void** shapes;
    int count;
};
int ShapeCount() { return reinterpret_cast<const CursorShapes*>(0x007C7BD8)->count; }
} // namespace

// 005DAEA0.
static void __fastcall SetUpCursorsVerify(bool modeTwo, int context, int smallCursors, int flags) {
    SetUpCursors(modeTwo, context, smallCursors, flags);
    ei::verify::Check("SetUpCursors", true, ShapeCount() >= 25, "%d shapes", ShapeCount());
}
EI_VERIFY(0x005DAEA0, SetUpCursorsVerify, 0, s_unused);

// 005DAE70.
static void __fastcall ResetCursorsVerify(bool modeTwo, int context, int smallCursors, int flags) {
    ResetCursors(modeTwo, context, smallCursors, flags);
    ei::verify::Check("ResetCursors", true, ShapeCount() >= 25, "%d shapes", ShapeCount());
}
EI_VERIFY(0x005DAE70, ResetCursorsVerify, 0, s_unused);
