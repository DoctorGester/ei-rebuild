#pragma once

// The switch-over device's questions without the DirectDraw / Direct3D headers (device.h has the
// rest; device_null.cpp): for the new renderer, which reads the game's device states from here when
// the device object was built without a device (ticket R9b, docs/notes/renderer.md section 9).

#include <cstdint>

namespace device {

// The render state / texture stage state the game last set (Direct3D 7's
// default before that).
std::uint32_t SwitchOverRenderState(std::uint32_t state);
std::uint32_t SwitchOverStageState(std::uint32_t stage, std::uint32_t state);

} // namespace device
