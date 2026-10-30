// The DirectInput mouse (DirectInputMouse): set up at the application's start,
// acquired with the window's activation and polled by the cursor (the device is platform/platform.h's). See ui_cursor.h.
#include "app.h"
#include "ui_cursor.h"

#include "lib/crt.h"

namespace {

const char* const kNoInput = "Can't create direct input object\n";        // "Can't create direct input object\n"
const char* const kNoDevice = "Can't create direct input device for mouse\n";       // "Can't create direct input device for mouse\n"
const char* const kNoFormat = "Can't set data format for mouse device\n";       // "Can't set data format for mouse device\n"
const char* const kNoCooperation = "Can't set cooperative level for mouse device\n";  // "Can't set cooperative level for mouse device\n"
const char* const kNoAcquire = "Can't acquire mouse device\n";      // "Can't acquire mouse device\n"

// The device's failed step reported (FatalErrorCode); its answer as 0 / 1.
int Reported(platform::MouseStep step, std::int32_t code) {
    static const char* const texts[] = {kNoInput, kNoDevice, kNoFormat, kNoCooperation, kNoAcquire};
    return app::FatalErrorCode(code, texts[static_cast<int>(step)]) != 0 ? 1 : 0;
}

} // namespace

int DirectInputMouse::Setup(platform::NativeWindow window, platform::NativeInstance instance) {
    return device.Open(window, instance, &Reported);
}

void DirectInputMouse::Shutdown() { device.Close(); }

int DirectInputMouse::SetActive(bool active) { return device.SetActive(active, &Reported); }

const std::int32_t* DirectInputMouse::Poll() {
    device.Read(state);
    return state;
}

// Run when the application drops the mouse at exit.
DirectInputMouse::~DirectInputMouse() { Shutdown(); }
