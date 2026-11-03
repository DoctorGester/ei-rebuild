// The image's harness runs open no sound device (audio::UseNoDevice): nothing is heard, and the voices'
// ends and the music's halves keep to the game's clock, the same on every machine and whatever device it has. Set
// before main when the run has a scenario (EI_HARNESS, as the harness itself starts: harness.cpp's Harness), unless
// EI_HARNESS_SOUND asks for the real device (a run that measures the sound with EI_AUDIO_TRACE; not for goldens: the
// music's halves then free at the device's pace).
#include "audio/audio.h"
#include "platform/platform.h"

namespace ei::verify::image {

namespace {

struct NoSoundDevice {
    NoSoundDevice() {
        if (!platform::EnvironmentVariable("EI_HARNESS").empty() && platform::EnvironmentVariable("EI_HARNESS_SOUND").empty())
            audio::UseNoDevice();
    }
} g_noSoundDevice;

} // namespace

} // namespace ei::verify::image
