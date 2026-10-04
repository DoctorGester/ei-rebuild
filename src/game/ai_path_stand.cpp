// A unit's path told to stand where the unit is (005B1DE0, 005B1F10, 005B2050, 005B2190; thiscall
// on the AIPath): the remaining cells are dropped, the order reset, the target becomes the unit's
// cell (position * 1 / (0073FD18 * 0.5)) with the flags {reached, -, -, 1}, the excluded regions
// emptied, and the order starts at the unit's position facing the direction its orientation
// quaternion (+0x34) gives, negated (computed on the FPU in the original's order). Then the
// variants add a turn (005C7C00), a tracked object (005C7C20) or a turn with an end (005C7C40);
// 005B2190 starts at the path's own time (+0x10). See ai_map.h (AIPath) and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

// The facing direction from the quaternion {a, b, c, d} at ECX (the unit's +0x34) into EDX:
// x = -2(cb - da) (rounded once), y = -(aa - (bb + dd + cc) + 2cc) and z = -2(cd + ba), the
// last two rounded to floats before the negation, as the original.
__declspec(naked) void __fastcall FacingFromQuaternion(const float* /*q*/, float* /*out*/) {
    __asm {
        sub esp, 8
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 4]
        fld dword ptr [ecx + 0Ch]
        fmul dword ptr [ecx]
        fsubp st(1), st
        fadd st, st
        fld dword ptr [ecx]
        fld dword ptr [ecx + 0Ch]
        fld dword ptr [ecx + 8]
        fld st(0)
        fmulp st(1), st
        fld dword ptr [ecx + 4]
        fld st(3)
        fmul st, st(4)
        fld st(3)
        fmul st, st(4)
        fld st(2)
        fmul st, st(3)
        faddp st(1), st
        fadd st, st(3)
        fsubp st(1), st
        fxch st(2)
        fadd st, st
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [esp]
        fstp st(0)
        fstp st(0)
        fstp st(0)
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 0Ch]
        fld dword ptr [ecx + 4]
        fmul dword ptr [ecx]
        faddp st(1), st
        fadd st, st
        fstp dword ptr [esp + 4]
        fchs
        fstp dword ptr [edx]
        fld dword ptr [esp]
        fchs
        fstp dword ptr [edx + 4]
        fld dword ptr [esp + 4]
        fchs
        fstp dword ptr [edx + 8]
        add esp, 8
        ret
    }
}

// 005B16D0 (RET 8): the excluded regions' CArray::SetSize.
__declspec(naked) void __fastcall ExcludedSetSize(void*, void*, int, int) EI_ORIGINAL(0x005B16D0)
// The order's follow-ups through their original addresses (ai_motion.cpp), the arguments as the
// caller's dwords.
__declspec(naked) void __fastcall OrderSetTurn(AIPath*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x005C7C00)
__declspec(naked) void __fastcall OrderSetTrack(AIPath*, void*, std::uint32_t, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x005C7C20)
__declspec(naked) void __fastcall OrderSetTurnUntil(AIPath*, void*, std::uint32_t, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x005C7C40)
__declspec(naked) bool __fastcall OrderStart(AIPath*, void*, const void*, const Vector3*, int) EI_ORIGINAL(0x005C7B00)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

#pragma float_control(precise, on, push)

// The common part; `time` is read after the reset (005B2190 passes the path's own +0x10).
void StandAt(AIPath* path, const int* time, std::uint8_t* unit) {
    path->cells.RemoveAll();
    path->Reset();
    *reinterpret_cast<std::uint32_t*>(&path->targetX) = 0;
    *reinterpret_cast<std::uint32_t*>(&path->targetY) = 0;
    path->reached = true;
    path->failed = false;
    path->flag6E = false;
    path->flag6F = true;
    ExcludedSetSize(reinterpret_cast<std::uint8_t*>(path) + 0x70, nullptr, 0, -1);
    path->smoothing = 0;
    path->reached = true;
    double scale = static_cast<double>(GameFloat(0x0073B84C)) /
                   (static_cast<double>(GameFloat(0x0073FD18)) * GameFloat(0x0073B86C));
    const float* position = reinterpret_cast<const float*>(unit + 0x1C);
    path->targetX = StoreFloat(static_cast<float>(scale * position[0]));
    path->targetY = StoreFloat(static_cast<float>(scale * position[1]));
    Vector3 facing;
    FacingFromQuaternion(reinterpret_cast<const float*>(unit + 0x34), &facing.x);
    OrderStart(path, nullptr, position, &facing, *time);
}

#pragma float_control(pop)

} // namespace

// 005B1DE0 (RET 0x10).
void __fastcall StandAndTurn(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t direction,
                             std::uint32_t rate) {
    StandAt(path, &time, unit);
    OrderSetTurn(path, nullptr, direction, rate);
}

// 005B1F10 (RET 0x14).
void __fastcall StandAndTrack(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t id, std::uint32_t from,
                              std::uint32_t to) {
    StandAt(path, &time, unit);
    OrderSetTrack(path, nullptr, id, from, to);
}

// 005B2050 (RET 0x14).
void __fastcall StandAndTurnUntil(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t direction,
                                  std::uint32_t a, std::uint32_t b) {
    StandAt(path, &time, unit);
    OrderSetTurnUntil(path, nullptr, direction, a, b);
}

// 005B2190 (RET 4).
void __fastcall StandStill(AIPath* path, void*, std::uint8_t* unit) {
    StandAt(path, reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(path) + 0x10), unit);
}

EI_REPLACE(0x005B1DE0, StandAndTurn);
EI_REPLACE(0x005B1F10, StandAndTrack);
EI_REPLACE(0x005B2050, StandAndTurnUntil);
EI_REPLACE(0x005B2190, StandStill);
