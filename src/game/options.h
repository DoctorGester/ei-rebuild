#pragma once

// The game options object (global at 0079BC70), its loader from the Windows registry (0045B210,
// called once by WinMain with the game's registry path) and the matching writer (0045C560). Names are provisional; see
// docs/notes/objects.md ("Options").
//
// The object starts with eight registry subkey names (CStrings) that the loader passes to the
// value getters; the values follow, 0x26C bytes in all. GameOptions names every field (A-E2); the
// loader, the writer and the defaults (older code) still go by the offsets in `field`.

#include "../lib/mfc.h"
#include "game_data.h"
#include "../lib/static_object.h"

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

// One camera movement's three settings ("CameraDefault<Scroll|Rotate|Pitch|Zoom>Speed",
// "...PowerMouse", "...PowerKeyboard").
struct CameraMotionOptions {
    float speed;
    float powerMouse;
    float powerKeyboard;
};
// One camera shortcut ("CameraShortcut <n> Distance / AxisX / AxisY / AxisZ / Angle / Pitch").
struct CameraShortcutOptions {
    float distance;
    float axisX;
    float axisY;
    float axisZ;
    float angle;
    float pitch;
};
// A light's colour (0..1; the registry holds 0..255) and radius.
struct LightOptions {
    float red;
    float green;
    float blue;
    float radius;
};

// The whole object, 0x26C bytes; the registry value names in comments (a member named after its
// value is not commented).
class GameOptions {
public:
    // 00458870 (options_construct.cpp; A-E2): what the static initialiser 00458700 builds before
    // WinMain: the eighteen strings and the first defaults (800 x 600, 16 bits; 00458DA0 sets the
    // game's real defaults later, before the registry is read).
    GameOptions();
    // 0045B210. Reads every option from the registry under HKEY_CURRENT_USER\<path>\<subkey>; a
    // missing value keeps the current one (the defaults set before).
    void LoadFromRegistry(const char* path);
    // 0045C560. Writes the same values back (WinMain calls it once, on exit). The camera flips
    // are written as booleans, the light colours as 0..255, "Joystick" always as 0, and
    // "Difficulty Level" from +0x254.
    void SaveToRegistry(const char* path);

    // The registry subkeys.
    mfc::CString keyStatistics_;     // +0x00 "Statistics Output Settings"
    mfc::CString keyGame_;           // +0x04 "Game Specific Settings"
    mfc::CString keyTerrain_;        // +0x08 "Terrain Settings"
    mfc::CString keyGeneral_;        // +0x0C "General Settings" (video and audio)
    mfc::CString keyCamera_;         // +0x10 "Camera Settings"
    mfc::CString keyShadows_;        // +0x14 "Shadow Settings"
    mfc::CString keyPaths_;          // +0x18 "Path Settings"
    mfc::CString keyNetwork_;        // +0x1C "Network Settings"
    mfc::CString workPath_;          // +0x20 Work path
    mfc::CString resourcesPath_;     // +0x24 Resources path
    mfc::CString cdRomPath_;         // +0x28 CD-ROM Path
    int driverIndex_;                // +0x2C DrawingTarget: the Direct3D driver (-1: the first hardware one)
    int resolutionX_;                // +0x30 VideoResolution
    int resolutionY_;                // +0x34
    int colourDepth_;                // +0x38 VideoColorDepthInBits
    int zBufferDepth_;               // +0x3C VideoZBufferDepthInBits
    int refreshRate_;                // +0x40 VideoRefreshRate
    int fullScreen_;                 // +0x44 FullScreen
    int joystick_;                   // +0x48
    int video_;                      // +0x4C
    int audio_;                      // +0x50
    int audioStreaming_;             // +0x54
    int audioSfx_;                   // +0x58
    int audioSpeech_;                // +0x5C
    int dithering_;                  // +0x60 Dithering
    int specular_;                   // +0x64 Specular
    int antialiasing_;               // +0x68 Antialiasing
    int textureQuality_;             // +0x6C TextureQuality
    int mipmapping_;                 // +0x70 Mipmapping (cleared when the driver has no mipmaps)
    int filtering_;                  // +0x74 Filtering: 0 point, 1 / 2 linear
    int perspectiveCorrection_;      // +0x78 PerspectiveCorrection
    int showParticles_;              // +0x7C
    int fpsIndependentCursor_;       // +0x80
    int automaticQuantization_;      // +0x84
    int movieCapturingFps_;          // +0x88
    float brightness_;               // +0x8C
    float contrast_;                 // +0x90
    float gamma_;                    // +0x94
    int reverseStereo_;              // +0x98 "Reverse Stereo"
    int shadowQuality_;              // +0x9C
    int heroShadow_;                 // +0xA0
    int unitShadow_;                 // +0xA4
    int buildingShadow_;             // +0xA8
    int floraShadow_;                // +0xAC
    int otherShadow_;                // +0xB0
    int shadowLightingFreq_;         // +0xB4 LightingFreq of the shadow settings
    int floraSpecialLight_;          // +0xB8
    float lod1_;                     // +0xBC
    float lod2_;                     // +0xC0
    float borderFogDistance_;        // +0xC4
    int enableWaterWaves_;           // +0xC8
    int enableSelfShadowing_;        // +0xCC
    int enableDynamicLights_;        // +0xD0
    int lightingFreq_;               // +0xD4 LightingFreq of the terrain settings
    int shadowingFreq_;              // +0xD8
    int enableFootprints_;           // +0xDC
    int enableBloodprints_;          // +0xE0
    int enableFireprints_;           // +0xE4
    int enableBloodprintsRelight_;   // +0xE8
    int createOutputInfo_;           // +0xEC
    int numberOfFrames_;             // +0xF0
    float cameraFlipX_;              // +0xF4 CameraFlipXAxis: -1 or 1
    float cameraFlipY_;              // +0xF8
    float cameraMinHeight_;          // +0xFC
    float cameraMaxHeight_;          // +0x100
    float cameraDefaultHeight_;      // +0x104
    float cameraDefaultXDeploy_;     // +0x108
    float cameraDefaultYDeploy_;     // +0x10C
    float cameraMinDistance_;        // +0x110 CameraMinDistanceToCarrier
    float cameraMaxDistance_;        // +0x114
    float cameraMinLimitDistance_;   // +0x118 CameraMinLimitDistanceToCarrier
    float cameraMaxLimitDistance_;   // +0x11C
    float cameraDefaultDistance_;    // +0x120 CameraDefaultDistanceToCarrier
    float briefingMinDistance_;      // +0x124 CameraBriefingMinDistanceToCarrier
    float briefingMaxDistance_;      // +0x128
    float briefingMinLimitDistance_; // +0x12C
    float briefingMaxLimitDistance_; // +0x130
    float briefingMinPitch_;         // +0x134 CameraBriefingMinPitch
    float briefingMaxPitch_;         // +0x138
    int cameraBorderScrollArea_;     // +0x13C
    int cameraFrameSelectionArea_;   // +0x140 CameraFrameSelectionSensetiveArea
    float cameraTerrainArea_;        // +0x144 CameraTerrainSensetiveArea
    CameraMotionOptions cameraScroll_;   // +0x148
    CameraMotionOptions cameraRotate_;   // +0x154
    CameraMotionOptions cameraPitch_;    // +0x160
    CameraMotionOptions cameraZoom_;     // +0x16C
    CameraShortcutOptions cameraShortcuts_[4];   // +0x178
    float cameraDefaultMinPitch_;    // +0x1D8
    float cameraDefaultMaxPitch_;    // +0x1DC
    float nearClipDistance_;         // +0x1E0
    float farClipDistance_;          // +0x1E4 FarClipDistance
    float defaultWindForce_;         // +0x1E8
    int defaultTimeScale_;           // +0x1EC
    int useSwCursors_;               // +0x1F0 UseSWCursors
    int selectionType_;              // +0x1F4
    float cornerSize_;               // +0x1F8
    int drawVisibleOnly_;            // +0x1FC
    int drawPath_;                   // +0x200
    int drawFlyingHp_;               // +0x204 DrawFlyingHP
    int showTutorial_;               // +0x208
    int autosave_;                   // +0x20C
    float toolTipTime_;              // +0x210 "ToolTip Time"
    int switchFilters_;              // +0x214
    mfc::CString cdLabels_[3];       // +0x218 "CD1 Label" ..
    mfc::CString fontCharSet_;       // +0x224 "Font CharSet"
    int keyboardLayout_;             // +0x228 "Keyboard Layout"
    float objectsLightingCoeff_;     // +0x22C
    LightOptions heroLight_;         // +0x230
    LightOptions questLight_;        // +0x240
    int difficulty_;                 // +0x250 "Difficulty Level"
    int difficultyStored_;           // +0x254 the same value: the one that is written back
    mfc::CString defaultMapName_;    // +0x258
    mfc::CString defaultStreamingSound_;   // +0x25C
    float fogNightStartDistance_;    // +0x260
    float fogDayStartDistance_;      // +0x264 FogDayStartDistance
    mfc::CString masterServerName_;  // +0x268 "Master Server Name"
};
static_assert(offsetof(GameOptions, driverIndex_) == 0x2C && offsetof(GameOptions, resolutionX_) == field::kResolutionX &&
              offsetof(GameOptions, fullScreen_) == 0x44 && offsetof(GameOptions, dithering_) == 0x60 &&
              offsetof(GameOptions, perspectiveCorrection_) == 0x78 && offsetof(GameOptions, farClipDistance_) == 0x1E4 &&
              offsetof(GameOptions, fogDayStartDistance_) == 0x264);
static_assert(offsetof(GameOptions, workPath_) == field::kWorkPath && offsetof(GameOptions, shadowQuality_) == 0x9C &&
              offsetof(GameOptions, lod1_) == 0xBC && offsetof(GameOptions, cameraFlipX_) == field::kCameraFlipX &&
              offsetof(GameOptions, cameraScroll_) == 0x148 && offsetof(GameOptions, cameraShortcuts_) == field::kCameraShortcuts &&
              offsetof(GameOptions, cameraDefaultMinPitch_) == 0x1D8 && offsetof(GameOptions, cdLabels_) == field::kCdLabels &&
              offsetof(GameOptions, heroLight_) == field::kHeroLight && offsetof(GameOptions, questLight_) == field::kQuestLight &&
              offsetof(GameOptions, difficulty_) == field::kDifficulty &&
              offsetof(GameOptions, masterServerName_) == field::kMasterServerName &&
              sizeof(GameOptions) == field::kLoadedSize);

// The game's options (the object at 0079BC70; options_global.cpp): built by the static initialiser 00458700 in the list's
// order, destroyed by the exit function it registers (00458720; options_construct.cpp, options_defaults.cpp).
extern lib::StaticObject<GameOptions> g_options;
inline GameOptions* TheOptions() { return &*g_options; }

// 00458DA0 (options_defaults.cpp). The options' defaults.
void __fastcall OptionsResetDefaults(GameOptions* self);

} // namespace options
