#pragma once

// The mouse's motion since the last read (docs/notes/platform.md): the game's cursor adds it to the position it takes
// from the cursor calls (platform/cursor.h), and the buttons come as window events. On Windows this is DirectInput 7's
// system mouse, non-exclusive, in the background.

#include "platform/window.h"

#include <cstdint>

struct IDirectInput7A;
struct IDirectInputDevice7A;

namespace platform {

enum class MouseStep : std::uint8_t {
    Create,       // the input object
    Device,       // the mouse device
    Format,       // its data format
    Cooperation,  // its cooperative level
    Acquire,      // taking the device
};

// Told of a step that failed, with the system's result code; the answer is the result of the call that was running
// (0 or 1), except for a failed Acquire in Open, whose answer is passed over.
using MouseReport = int (*)(MouseStep step, std::int32_t code);

class MouseDevice {
public:
    MouseDevice() = default;
    ~MouseDevice();
    MouseDevice(const MouseDevice&) = delete;
    MouseDevice& operator=(const MouseDevice&) = delete;

    // The input object for `instance`, its system mouse with the mouse data format, for `window`, taken. A failed
    // step is reported and ends the call with the report's answer (the earlier steps' objects stay); 1 otherwise.
    int Open(NativeWindow window, NativeInstance instance, MouseReport report);
    void Close();
    // Takes the device or lets it go with the window's activation; 0 without a device, the report's answer when
    // taking it fails, else 1.
    int SetActive(bool active, MouseReport report);
    // The state: the motion in x, y and the wheel, and the buttons (four values; zeroed when the read fails).
    void Read(std::int32_t state[4]);

private:
    IDirectInput7A* input = nullptr;         // counted
    IDirectInputDevice7A* device = nullptr;  // counted
};

} // namespace platform

// The harness's view of the DirectInput object (verify builds): Open passes it each one it makes, before the mouse
// device. In game.exe's runs the harness routes the original's import slot instead.
extern void (*g_directInputObserver)(IDirectInput7A* input);
