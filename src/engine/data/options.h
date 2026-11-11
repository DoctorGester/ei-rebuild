#pragma once

// The game options object (a global), its loader from the settings file (settings.h; called once by WinMain) and
// the matching writer. See docs/notes/app.md, "Options".

#include "lib/scales.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace options {

// One camera movement's three settings ("CameraDefault<Scroll|Rotate|Pitch|Zoom>Speed",
// "...PowerMouse", "...PowerKeyboard").
struct CameraMotionOptions {
    float speed = 0.0f;
    float powerMouse = 0.0f;
    float powerKeyboard = 0.0f;
};
// One camera shortcut ("CameraShortcut <n> Distance / AxisX / AxisY / AxisZ / Angle / Pitch").
struct CameraShortcutOptions {
    float distance = 0.0f;
    float axisX = 0.0f;
    float axisY = 0.0f;
    float axisZ = 0.0f;
    float angle = 0.0f;
    float pitch = 0.0f;
};
// A light's colour (0..1) and radius.
struct LightOptions {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float radius = 0.0f;
};

// What the settings used to hold and no longer do: the values the game always runs with.
inline constexpr float kObjectsLightingCoeff = 2.0f;   // the sun's and the lights' strength on units, buildings and trees
inline constexpr LightOptions kHeroLight = {216.0f * lib::kByteScale, 216.0f * lib::kByteScale,
                                            116.0f * lib::kByteScale, 5.0f};
inline constexpr LightOptions kQuestLight = {120.0f * lib::kByteScale, 72.0f * lib::kByteScale,
                                             120.0f * lib::kByteScale, 5.0f};
inline constexpr float kDefaultWindForce = 0.4f;     // the strongest wind a map may have
inline constexpr int kDefaultTimeScale = 60;         // the day and night clock: bigger is slower
inline constexpr const char* kDefaultMapName = "Zone1.mpr";
inline constexpr const char* kDefaultStreamingSound = "melody.mp3";

// The whole object; the settings file's value names in comments (a member named after its value is not commented).
// The sections: options.cpp's tables.
class GameOptions {
public:
    // Reads every option from the settings file; a missing value takes its default (what each option does and its
    // default: src/engine/data/settings_catalog.inc).
    void Load();
    // Writes the same values back and saves the file (WinMain calls it once, on exit). The camera flips are written
    // as booleans and "Difficulty Level" from difficultyStored.
    void Save();

    std::string workPath;           // Work path
    std::string resourcesPath;      // Resources path
    int resolutionX = 800;           // VideoResolution; 800x600 when its value is not a size
    int resolutionY = 600;
    int fullScreen = 0;              // FullScreen
    int video = 0;
    int audio = 0;
    int audioStreaming = 0;
    int audioSfx = 0;
    int audioSpeech = 0;
    float brightness = 0.0f;
    float gamma = 0.0f;
    int reverseStereo = 0;           // "Reverse Stereo"
    int shadows = 0;                 // Shadows: every shadow on or off
    float borderFogDistance = 0.0f;
    float cameraFlipX = 0.0f;        // CameraFlipXAxis: -1 or 1
    float cameraFlipY = 0.0f;
    float cameraMinHeight = 0.0f;
    float cameraMaxHeight = 0.0f;
    float cameraDefaultHeight = 0.0f;
    float cameraDefaultXDeploy = 0.0f;
    float cameraDefaultYDeploy = 0.0f;
    float cameraMinDistance = 0.0f;  // CameraMinDistanceToCarrier
    float cameraMaxDistance = 0.0f;
    float cameraMinLimitDistance = 0.0f;  // CameraMinLimitDistanceToCarrier
    float cameraMaxLimitDistance = 0.0f;
    float cameraDefaultDistance = 0.0f;  // CameraDefaultDistanceToCarrier
    float briefingMinDistance = 0.0f;  // CameraBriefingMinDistanceToCarrier
    float briefingMaxDistance = 0.0f;
    float briefingMinLimitDistance = 0.0f;
    float briefingMaxLimitDistance = 0.0f;
    float briefingMinPitch = 0.0f;  // CameraBriefingMinPitch
    float briefingMaxPitch = 0.0f;
    int cameraBorderScrollArea = 0;
    int cameraFrameSelectionArea = 0;  // CameraFrameSelectionSensetiveArea
    float cameraTerrainArea = 0.0f;  // CameraTerrainSensetiveArea
    CameraMotionOptions cameraScroll;
    CameraMotionOptions cameraRotate;
    CameraMotionOptions cameraPitch;
    CameraMotionOptions cameraZoom;
    CameraShortcutOptions cameraShortcuts[4];
    float cameraDefaultMinPitch = 0.0f;
    float cameraDefaultMaxPitch = 0.0f;
    float nearClipDistance = 0.0f;
    int selectionType = 0;
    int drawVisibleOnly = 1;         // not a setting: 1 draws only the units the party sees (the spectator switch turns it off)
    int drawPath = 0;
    int drawFlyingHp = 0;            // DrawFlyingHP
    int showTutorial = 0;
    int autosave = 0;
    float toolTipTime = 0.0f;        // "ToolTip Time"
    int switchFilters = 0;
    int difficulty = 0;              // "Difficulty Level"
    int difficultyStored = 0;        // the same value: the one that is written back
    std::string masterServerName;   // "Master Server Name"
};
// The game's options (data/options.cpp).
extern GameOptions g_options;

} // namespace options
