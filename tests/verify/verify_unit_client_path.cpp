// Checks for the client controller's path placement (src/game/unit_client_path.cpp). The original
// runs first (its move and turn are the game's); the unit's orientation (+0x34) and the controller's
// snap byte (+0x76) are put back, ours runs; the position, orientation, animation speed factor
// (+0x178, all within a few ULPs: rule 7) and the snap byte are compared. The position is not put
// back: the world's move keeps the sector grid in step with it, and the motion sample rewrites it.
#include "verify.h"

#include <cstdint>
#include <cstring>

void __fastcall ClientPlaceOnPath(std::uint8_t* controller, void*, float time, float rate);

namespace {
template <typename T>
T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 8, 1e-5f); }
} // namespace

// 00535850: PUSH EBP; MOV EBP,ESP; SUB ESP,0x48 (6 bytes).
using PlaceFn = void(__fastcall*)(std::uint8_t*, void*, float, float);
static PlaceFn s_place;
static void __fastcall ClientPlaceOnPathVerify(std::uint8_t* controller, void*, float time, float rate) {
    std::uint8_t* unit = Field<std::uint8_t*>(controller, 0x1C);
    float orientation[4];
    std::memcpy(orientation, unit + 0x34, sizeof(orientation));
    std::uint8_t snap = controller[0x76];

    s_place(controller, nullptr, time, rate);
    float theirs[3 + 4 + 1];
    std::memcpy(theirs, unit + 0x1C, 12);
    std::memcpy(theirs + 3, unit + 0x34, 16);
    theirs[7] = Field<float>(unit, 0x178);
    std::uint8_t theirsSnap = controller[0x76];

    std::memcpy(unit + 0x34, orientation, sizeof(orientation));
    controller[0x76] = snap;
    ClientPlaceOnPath(controller, nullptr, time, rate);
    float mine[3 + 4 + 1];
    std::memcpy(mine, unit + 0x1C, 12);
    std::memcpy(mine + 3, unit + 0x34, 16);
    mine[7] = Field<float>(unit, 0x178);

    int differs = -1;
    for (int i = 0; i < 8 && differs < 0; ++i)
        if (!Near(theirs[i], mine[i])) differs = i;
    bool ok = differs < 0 && theirsSnap == controller[0x76];
    static const char* const kNames[] = {"x", "y", "z", "q0", "q1", "q2", "q3", "speed factor"};
    ei::verify::Check("ClientPlaceOnPath", true, ok, "time %g rate %g: %s %g vs %g", time, rate,
                      differs < 0 ? "snap byte" : kNames[differs], differs < 0 ? 0.0 : theirs[differs],
                      differs < 0 ? 0.0 : mine[differs]);
}
EI_VERIFY(0x00535850, ClientPlaceOnPathVerify, 6, s_place);
