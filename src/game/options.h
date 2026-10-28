#pragma once

// The game options object (a global), its loader from the Windows registry
// (called once by WinMain with the game's registry path) and the matching writer. Names are provisional; see
// docs/notes/objects.md ("Options").
// The object starts with eight registry subkey names that the loader passes to the value getters;
// the values follow.

#include "game_data.h"
#include "../lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace options {

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

// The whole object; the registry value names in comments (a member named after its
// value is not commented).
class GameOptions {
public:
    // What the static initialiser builds before
    // WinMain: the eighteen strings and the first defaults (800 x 600, 16 bits; the defaults function
    // sets the game's real defaults later, before the registry is read).
    GameOptions();
    // Reads every option from the registry under HKEY_CURRENT_USER\<path>\<subkey>; a
    // missing value keeps the current one (the defaults set before).
    void LoadFromRegistry(const char* path);
    // Writes the same values back (WinMain calls it once, on exit). The camera flips are written as booleans,
    // the light colours as 0..255, "Joystick" always as 0, and "Difficulty Level" from +0x254.
    void SaveToRegistry(const char* path);

    // The registry subkeys.
    std::string keyStatistics_;     // "Statistics Output Settings"
    std::string keyGame_;           // "Game Specific Settings"
    std::string keyTerrain_;        // "Terrain Settings"
    std::string keyGeneral_;        // "General Settings" (video and audio)
    std::string keyCamera_;         // "Camera Settings"
    std::string keyShadows_;        // "Shadow Settings"
    std::string keyPaths_;          // "Path Settings"
    std::string keyNetwork_;        // "Network Settings"
    std::string workPath_;          // Work path
    std::string resourcesPath_;     // Resources path
    std::string cdRomPath_;         // CD-ROM Path
    int driverIndex_;                // DrawingTarget: the Direct3D driver (-1: the first hardware one)
    int resolutionX_;                // VideoResolution
    int resolutionY_;
    int colourDepth_;                // VideoColorDepthInBits
    int zBufferDepth_;               // VideoZBufferDepthInBits
    int refreshRate_;                // VideoRefreshRate
    int fullScreen_;                 // FullScreen
    int joystick_;
    int video_;
    int audio_;
    int audioStreaming_;
    int audioSfx_;
    int audioSpeech_;
    int dithering_;                  // Dithering
    int specular_;                   // Specular
    int antialiasing_;               // Antialiasing
    int textureQuality_;             // TextureQuality
    int mipmapping_;                 // Mipmapping (cleared when the driver has no mipmaps)
    int filtering_;                  // Filtering: 0 point, 1 / 2 linear
    int perspectiveCorrection_;      // PerspectiveCorrection
    int showParticles_;
    int fpsIndependentCursor_;
    int automaticQuantization_;
    int movieCapturingFps_;
    float brightness_;
    float contrast_;
    float gamma_;
    int reverseStereo_;              // "Reverse Stereo"
    int shadowQuality_;
    int heroShadow_;
    int unitShadow_;
    int buildingShadow_;
    int floraShadow_;
    int otherShadow_;
    int shadowLightingFreq_;         // LightingFreq of the shadow settings
    int floraSpecialLight_;
    float lod1_;
    float lod2_;
    float borderFogDistance_;
    int enableWaterWaves_;
    int enableSelfShadowing_;
    int enableDynamicLights_;
    int lightingFreq_;               // LightingFreq of the terrain settings
    int shadowingFreq_;
    int enableFootprints_;
    int enableBloodprints_;
    int enableFireprints_;
    int enableBloodprintsRelight_;
    int createOutputInfo_;
    int numberOfFrames_;
    float cameraFlipX_;              // CameraFlipXAxis: -1 or 1
    float cameraFlipY_;
    float cameraMinHeight_;
    float cameraMaxHeight_;
    float cameraDefaultHeight_;
    float cameraDefaultXDeploy_;
    float cameraDefaultYDeploy_;
    float cameraMinDistance_;        // CameraMinDistanceToCarrier
    float cameraMaxDistance_;
    float cameraMinLimitDistance_;   // CameraMinLimitDistanceToCarrier
    float cameraMaxLimitDistance_;
    float cameraDefaultDistance_;    // CameraDefaultDistanceToCarrier
    float briefingMinDistance_;      // CameraBriefingMinDistanceToCarrier
    float briefingMaxDistance_;
    float briefingMinLimitDistance_;
    float briefingMaxLimitDistance_;
    float briefingMinPitch_;         // CameraBriefingMinPitch
    float briefingMaxPitch_;
    int cameraBorderScrollArea_;
    int cameraFrameSelectionArea_;   // CameraFrameSelectionSensetiveArea
    float cameraTerrainArea_;        // CameraTerrainSensetiveArea
    CameraMotionOptions cameraScroll_;
    CameraMotionOptions cameraRotate_;
    CameraMotionOptions cameraPitch_;
    CameraMotionOptions cameraZoom_;
    CameraShortcutOptions cameraShortcuts_[4];
    float cameraDefaultMinPitch_;
    float cameraDefaultMaxPitch_;
    float nearClipDistance_;
    float farClipDistance_;          // FarClipDistance
    float defaultWindForce_;
    int defaultTimeScale_;
    int useSwCursors_;               // UseSWCursors
    int selectionType_;
    float cornerSize_;
    int drawVisibleOnly_;
    int drawPath_;
    int drawFlyingHp_;               // DrawFlyingHP
    int showTutorial_;
    int autosave_;
    float toolTipTime_;              // "ToolTip Time"
    int switchFilters_;
    std::string cdLabels_[3];       // "CD1 Label" ..
    std::string fontCharSet_;       // "Font CharSet"
    int keyboardLayout_;             // "Keyboard Layout"
    float objectsLightingCoeff_;
    LightOptions heroLight_;
    LightOptions questLight_;
    int difficulty_;                 // "Difficulty Level"
    int difficultyStored_;           // the same value: the one that is written back
    std::string defaultMapName_;
    std::string defaultStreamingSound_;
    float fogNightStartDistance_;
    float fogDayStartDistance_;      // FogDayStartDistance
    std::string masterServerName_;  // "Master Server Name"
};
// The game's options (options_global.cpp): built by its static initialiser in the list's
// order, destroyed by the exit function it registers (options_construct.cpp, options_defaults.cpp).
extern lib::StaticObject<GameOptions> g_options;
inline GameOptions* TheOptions() { return &*g_options; }

// The options' defaults.
void OptionsResetDefaults(GameOptions* self);

} // namespace options
