// game.exe's addresses as the harness names them.
//
// The harness names some of the game's tables by game.exe's address (`&OriginalGameData<const std::uint8_t>(address)`:
// the windows' and the AI modes' kinds, harness_windows.h and harness_digest.cpp), the ids the goldens hold.
#pragma once

#include <cstdint>

namespace ei {

// game.exe's object at its address. Only its address is taken (an id), never read. named_data.py lets a literal
// through only as this call's argument, and only in tests/.
template <typename T>
T& OriginalGameData(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

} // namespace ei
