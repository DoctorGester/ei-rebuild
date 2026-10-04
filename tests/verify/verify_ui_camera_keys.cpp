// Check for the battle camera's key input (ui_camera_keys.cpp).
//
// Side by side: the handler writes only its own fields (+0x08, +0x20..+0x2B) and the camera's
// motion, view and saved-view fields. Both are copied, the original runs, its result is copied
// and the old bytes put back, ours runs, and both copies must be equal.
#include "verify.h"

#include "../../src/game/ui_camera_keys.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uintptr_t kCamera = 0x0079B2A8;
constexpr std::size_t kCameraBytes = 0x1E0;   // up to the end of the saved views
constexpr std::size_t kKeysBytes = 0x2C;

struct State {
    std::uint8_t camera[kCameraBytes - 4];
    std::uint8_t keys[kKeysBytes - 4];
    void Read(const void* object) {
        std::memcpy(camera, reinterpret_cast<const std::uint8_t*>(kCamera) + 4, sizeof(camera));
        std::memcpy(keys, static_cast<const std::uint8_t*>(object) + 4, sizeof(keys));
    }
    void Write(void* object) const {
        std::memcpy(reinterpret_cast<std::uint8_t*>(kCamera) + 4, camera, sizeof(camera));
        std::memcpy(static_cast<std::uint8_t*>(object) + 4, keys, sizeof(keys));
    }
    int FirstDifference(const State& o) const {
        for (std::size_t i = 0; i < sizeof(camera); ++i)
            if (camera[i] != o.camera[i]) return static_cast<int>(i + 4);
        for (std::size_t i = 0; i < sizeof(keys); ++i)
            if (keys[i] != o.keys[i]) return 0x1000 + static_cast<int>(i + 4);
        return -1;
    }
};

} // namespace

// 005EADD0: MOV EAX,[ESP+0xC]; SUB ESP,0x10 (7).
static void(__thiscall* s_cameraKeys)(void*, int, int, int);
static void __fastcall CameraKeysVerify(void* self, void*, int key, int repeat, int flags) {
    static State before, expected, actual;
    before.Read(self);
    s_cameraKeys(self, key, repeat, flags);
    expected.Read(self);
    before.Write(self);
    CameraKeysOnKeyDown(self, nullptr, key, repeat, flags);
    actual.Read(self);
    int at = expected.FirstDifference(actual);
    ei::verify::Check("CameraKeys::OnKeyDown", true, at == -1, "scan %X: first difference at %X (0x1000+: keys object)",
                      flags & 0x1FF, at);
}
EI_VERIFY(0x005EADD0, CameraKeysVerify, 7, s_cameraKeys);
