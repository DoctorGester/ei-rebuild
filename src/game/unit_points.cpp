// Two CUnit virtual slots giving points of the unit (vtable 0073EA8C, shared by CFakeUnit
// 0073BFE8): 52 the position predicted along its path (0052FCC0) and 53 the point its current
// animation acts on (0052FDF0). Registered as free functions (thiscall as fastcall; rule 4). See
// docs/notes/units.md ("Unit points").
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// The unit's world [00455950]; the motion's sample [005C7430 on +0x27C: world, order, time,
// turning, position (half-cells), direction, speed, cell x, cell y]; the animation entry of an
// animation index [00518F80 (index)]; the orientation's axes [0040D940 X, 0040F560 Y (out)]; the AI
// map's passability for a size class [005BA790 (class, point)]; the debug message [0044C470].
__declspec(naked) std::uint8_t* __fastcall ObjectWorld(void*) EI_ORIGINAL(0x00455950)
__declspec(naked) bool __fastcall MotionSample(void*, void*, void*, void*, float, bool*, float*, float*, float*, int*,
                                               int*) EI_ORIGINAL(0x005C7430)
__declspec(naked) std::uint8_t* __fastcall AnimationEntry(void*, void*, std::uint32_t) EI_ORIGINAL(0x00518F80)
__declspec(naked) float* __fastcall AxisX(const void*, void*, float*) EI_ORIGINAL(0x0040D940)
__declspec(naked) float* __fastcall AxisY(const void*, void*, float*) EI_ORIGINAL(0x0040F560)
__declspec(naked) bool __fastcall IsPassableForClass(void*, void*, int, const float*) EI_ORIGINAL(0x005BA790)
__declspec(naked) void __cdecl DebugMessage(const char*, ...) EI_ORIGINAL(0x0044C470)

constexpr int kSlotPath = 0x40 / 4;           // controller: () -> its motion order
constexpr int kSlotIsServer = 0xDC / 4;       // unit: ()
constexpr int kSlotActionPoint = 0xD4 / 4;    // unit: (out) — slot 53
constexpr int kSlotWorldActive = 0x68 / 4;    // world: ()
constexpr int kOrderPath = 2;
constexpr float kCellScale = 1.0f;            // 0073E9B8 / 0073EBF0
constexpr float kHalf = 0.5f;                 // 0073B86C
const char* const kNoEntry = reinterpret_cast<const char*>(0x0078F918);   // "Can't find animation entry index..."

} // namespace

// 0052FCC0 (slot 52, RET 8). The server's unit gives its action point (slot 53); on a client a
// unit following a path order (kind 2) without flag 0x8000000 gives its motion's position at
// `time`; otherwise its position.
float* __fastcall UnitPredictPosition(std::uint8_t* unit, void*, float* out, int time) {
    void* order = ei::VCall<void*>(At<void*>(unit, 0x240), kSlotPath);
    if (ei::VCall<int>(unit, kSlotIsServer)) {
        ei::VCall<float*>(unit, kSlotActionPoint, out);
        return out;
    }
    if (At<int>(order, 0) == kOrderPath && !(At<std::uint32_t>(unit, 8) & 0x8000000)) {
        float toCells = 1.0f / (kCellScale * kHalf);
        float position[3] = {At<float>(unit, 0x1C), At<float>(unit, 0x20), At<float>(unit, 0x24)};
        position[0] = toCells * position[0];
        position[1] = toCells * position[1];
        bool turning;
        float direction[3];
        float speed;
        int cellX, cellY;
        MotionSample(unit + 0x27C, nullptr, ObjectWorld(unit), order, static_cast<float>(time), &turning, position,
                     direction, &speed, &cellX, &cellY);
        float toWorld = kCellScale * kHalf;
        out[0] = toWorld * position[0];
        out[1] = toWorld * position[1];
        std::memcpy(&out[2], &position[2], 4);
        return out;
    }
    std::memcpy(out, unit + 0x1C, 12);
    return out;
}

// 0052FDF0 (slot 53, RET 4; returns `out` in EAX, which the callers use). On the server, the
// point the unit's current animation (+0x254) acts on: an animation entry of kind 0x1C0000 (bits
// 0x3C0000 of +0x14) offsets the position along the orientation by its direction bits (0x1C00000
// of +0x18): 0x400000 back (-Y), 0x800000 forward (+Y) by +0x148, 0xC00000 right (+X), 0x1000000
// left (-X) by +0x144. The point is kept only where the AI map lets size class 3 stand (when the
// world is live); otherwise, and on a client, the position.
float* __fastcall UnitActionPoint(std::uint8_t* unit, void*, float* out) {
    if (!ei::VCall<int>(unit, kSlotIsServer)) {
        std::memcpy(out, unit + 0x1C, 12);
        return out;
    }
    std::uint8_t index = At<std::uint8_t>(unit, 0x254);
    float offset[3] = {0.0f, 0.0f, 0.0f};
    std::uint8_t* entry = AnimationEntry(unit, nullptr, index);
    if (!entry) {
        DebugMessage(kNoEntry, static_cast<unsigned>(index), At<const char*>(unit, 0xA4));
    } else if ((At<std::uint32_t>(entry, 0x14) & 0x3C0000) == 0x1C0000) {
        std::uint32_t side = At<std::uint32_t>(entry, 0x18) & 0x1C00000;
        float axis[3];
        if (side == 0x800000) {
            AxisY(unit + 0x34, nullptr, axis);
            float reach = At<float>(unit, 0x148);
            for (int i = 0; i < 3; ++i) offset[i] = axis[i] * reach;
        } else if (side == 0x400000) {
            AxisY(unit + 0x34, nullptr, axis);
            float reach = At<float>(unit, 0x148);
            for (int i = 0; i < 3; ++i) offset[i] = -axis[i] * reach;
        } else if (side == 0xC00000) {
            AxisX(unit + 0x34, nullptr, axis);
            float reach = At<float>(unit, 0x144);
            for (int i = 0; i < 3; ++i) offset[i] = axis[i] * reach;
        } else if (side == 0x1000000) {
            AxisX(unit + 0x34, nullptr, axis);
            float reach = At<float>(unit, 0x144);
            for (int i = 0; i < 3; ++i) offset[i] = -axis[i] * reach;
        }
    }
    std::uint8_t* world = ObjectWorld(unit);
    if (ei::VCall<int>(world, kSlotWorldActive)) {
        float point[3] = {offset[0] + At<float>(unit, 0x1C), offset[1] + At<float>(unit, 0x20),
                          offset[2] + At<float>(unit, 0x24)};
        if (!IsPassableForClass(world + 0x170, nullptr, 3, point)) {
            std::memcpy(out, unit + 0x1C, 12);
            return out;
        }
    }
    out[0] = offset[0] + At<float>(unit, 0x1C);
    out[1] = offset[1] + At<float>(unit, 0x20);
    out[2] = offset[2] + At<float>(unit, 0x24);
    return out;
}

EI_REPLACE(0x0052FCC0, UnitPredictPosition);
EI_REPLACE(0x0052FDF0, UnitActionPoint);
