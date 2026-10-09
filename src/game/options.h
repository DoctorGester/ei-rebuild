#pragma once

// The game options object (global at 0079BC70), its loader from the Windows registry (0045B210,
// called once by WinMain with the game's registry path) and the matching writer (0045C560). Names are provisional; see
// docs/notes/objects.md ("Options").
//
// The object starts with eight registry subkey names (CStrings) that the loader passes to the
// value getters; the values follow. Only the loader's fields are named here; the rest of the
// object (0x26C bytes are read or written by the loader) is owned by its users.

#include "../lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace options {

// Offsets in the options object.
namespace field {
constexpr std::size_t kKeyGeneral = 0x00;     // CString subkey: CreateOutputInfo, NumberOfFrames
constexpr std::size_t kKeyGame = 0x04;        // game settings, CD labels, lights, difficulty
constexpr std::size_t kKeyRender = 0x08;      // LOD, fog, prints, lighting
constexpr std::size_t kKeyVideo = 0x0C;       // video and audio
constexpr std::size_t kKeyCamera = 0x10;
constexpr std::size_t kKeyShadows = 0x14;
constexpr std::size_t kKeyPaths = 0x18;       // Work / Resources / CD-ROM path
constexpr std::size_t kKeyNetwork = 0x1C;     // Master Server Name
constexpr std::size_t kWorkPath = 0x20;       // CString
constexpr std::size_t kResourcesPath = 0x24;  // CString
constexpr std::size_t kCdRomPath = 0x28;      // CString
constexpr std::size_t kResolutionX = 0x30;    // from "VideoResolution" ("<x>x<y>")
constexpr std::size_t kResolutionY = 0x34;
constexpr std::size_t kCameraFlipX = 0xF4;    // float, -1 or 1
constexpr std::size_t kCameraFlipY = 0xF8;
constexpr std::size_t kCameraShortcuts = 0x178;   // 4 x {distance, x, y, z, angle, pitch} floats
constexpr std::size_t kCdLabels = 0x218;      // 3 CStrings
constexpr std::size_t kFontCharSet = 0x224;   // CString
constexpr std::size_t kHeroLight = 0x230;     // r, g, b (0..1), radius
constexpr std::size_t kQuestLight = 0x240;    // r, g, b (0..1), radius
constexpr std::size_t kDifficulty = 0x250;    // and +0x254: both get "Difficulty Level"
constexpr std::size_t kDefaultMapName = 0x258;          // CString
constexpr std::size_t kDefaultStreamingSound = 0x25C;   // CString
constexpr std::size_t kMasterServerName = 0x268;        // CString
constexpr std::size_t kLoadedSize = 0x26C;    // the bytes the loader reads or writes
} // namespace field

// The CString fields of the loaded range.
constexpr std::size_t kStringFields[] = {
    field::kKeyGeneral, field::kKeyGame, field::kKeyRender, field::kKeyVideo, field::kKeyCamera,
    field::kKeyShadows, field::kKeyPaths, field::kKeyNetwork, field::kWorkPath, field::kResourcesPath,
    field::kCdRomPath, field::kCdLabels, field::kCdLabels + 4, field::kCdLabels + 8, field::kFontCharSet,
    field::kDefaultMapName, field::kDefaultStreamingSound, field::kMasterServerName,
};

class GameOptions {
public:
    // 0045B210. Reads every option from the registry under HKEY_CURRENT_USER\<path>\<subkey>; a
    // missing value keeps the current one (the defaults set before).
    void LoadFromRegistry(const char* path);
    // 0045C560. Writes the same values back (WinMain calls it once, on exit). The camera flips
    // are written as booleans, the light colours as 0..255, "Joystick" always as 0, and
    // "Difficulty Level" from +0x254.
    void SaveToRegistry(const char* path);

    // The fields the renderer's creation (00500B70) reads; the registry value names in comments.
    std::uint8_t unknown_00[0x2C];   // the subkey names and paths (field::kKeyGeneral ..)
    int driverIndex_;                // +0x2C the Direct3D driver (-1: the first hardware one)
    int resolutionX_;                // +0x30 VideoResolution
    int resolutionY_;                // +0x34
    int colourDepth_;                // +0x38 VideoColorDepthInBits
    int zBufferDepth_;               // +0x3C VideoZBufferDepthInBits
    int refreshRate_;                // +0x40 VideoRefreshRate
    int fullScreen_;                 // +0x44 FullScreen
    std::uint8_t unknown_48[0x60 - 0x48];
    int dithering_;                  // +0x60 Dithering
    int specular_;                   // +0x64 Specular
    int antialiasing_;               // +0x68 Antialiasing
    int textureQuality_;             // +0x6C TextureQuality
    int mipmapping_;                 // +0x70 Mipmapping (cleared when the driver has no mipmaps)
    int filtering_;                  // +0x74 Filtering: 0 point, 1 / 2 linear
    int perspectiveCorrection_;      // +0x78 PerspectiveCorrection
    std::uint8_t unknown_7C[0x1E4 - 0x7C];
    float farClipDistance_;          // +0x1E4 FarClipDistance
    std::uint8_t unknown_1E8[0x264 - 0x1E8];
    float fogDayStartDistance_;      // +0x264 FogDayStartDistance
};
static_assert(offsetof(GameOptions, driverIndex_) == 0x2C && offsetof(GameOptions, resolutionX_) == field::kResolutionX &&
              offsetof(GameOptions, fullScreen_) == 0x44 && offsetof(GameOptions, dithering_) == 0x60 &&
              offsetof(GameOptions, perspectiveCorrection_) == 0x78 && offsetof(GameOptions, farClipDistance_) == 0x1E4 &&
              offsetof(GameOptions, fogDayStartDistance_) == 0x264);

inline GameOptions* TheOptions() { return reinterpret_cast<GameOptions*>(0x0079BC70); }

} // namespace options
