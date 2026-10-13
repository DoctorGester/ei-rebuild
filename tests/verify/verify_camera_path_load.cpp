// Check for camera_path_load.cpp (A-E1). Main thread; vanilla launch only (Rebirth patches 0040ECD0).
// Side by side on the camera itself: the original loads the file into the keys, which are copied,
// then ours loads it again (the array keeps its size, the file is read over the keys and the
// lead-in is settled anew, so the keys end as after one load); results, counts and key bytes are
// compared. Reached when a recorded camera path is played (the camera commands' glue 00578820 /
// 00578880), by reading. Not run.
#include "verify.h"

#include "../../src/game/camera.h"

#include <cstring>
#include <vector>

namespace {
// 0040ECD0: PUSH -1 (2), PUSH imm32 (5).
int(__fastcall* s_loadPath)(CCamera*, void*, const char*, int);
int __fastcall LoadPathVerify(CCamera* self, void*, const char* name, int settle) {
    int expected = s_loadPath(self, nullptr, name, settle);
    int expectedCount = self->keyCount_;
    std::vector<CameraKey> expectedKeys;
    if (self->keys_) expectedKeys.assign(self->keys_, self->keys_ + expectedCount);

    int actual = self->LoadPath(name, settle);
    ei::verify::Check("CCamera::LoadPath", expected, actual, "%s, settle %d", name, settle);
    ei::verify::Check("CCamera::LoadPath", expectedCount, self->keyCount_, "%s: key count", name);
    bool same = expectedCount == self->keyCount_ &&
                (expectedCount == 0 || std::memcmp(expectedKeys.data(), self->keys_,
                                                   static_cast<std::size_t>(expectedCount) * sizeof(CameraKey)) == 0);
    ei::verify::Check("CCamera::LoadPath", true, same, "%s: keys", name);
    return actual;
}
} // namespace

EI_VERIFY(0x0040ECD0, LoadPathVerify, 7, s_loadPath);
