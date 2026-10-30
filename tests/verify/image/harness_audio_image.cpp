// The image's harness runs open no sound device (platform::audio::UseNoDevice): nothing is heard, and the voices'
// ends and the music's halves keep to the game's clock, the same on every machine and whatever device it has. Set
// before main when the run has a scenario (EI_HARNESS, as the harness itself starts: harness.cpp's Harness).
#include "platform/audio.h"

#include <windows.h>

namespace ei::verify::image {

namespace {

struct NoSoundDevice {
    NoSoundDevice() {
        char path[MAX_PATH];
        const DWORD n = GetEnvironmentVariableA("EI_HARNESS", path, MAX_PATH);
        if (n != 0 && n < MAX_PATH) platform::audio::UseNoDevice();
    }
} g_noSoundDevice;

} // namespace

} // namespace ei::verify::image
