#pragma once

// The game options object (a global), its loader from the settings file (settings.h; called once by WinMain) and
// the matching writer. See docs/notes/app.md, "Options".

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
// A light's colour (0..1) and radius.
struct LightOptions {
    float red;
    float green;
    float blue;
    float radius;
};

// What the settings used to hold and no longer do: the values the game always runs with.
inline constexpr float kOver255 = 1.0f / 255.0f;   // (0x3B808081)
inline constexpr float kObjectsLightingCoeff = 2.0f;   // the sun's and the lights' strength on units, buildings and trees
inline constexpr LightOptions kHeroLight = {216.0f * kOver255, 216.0f * kOver255, 116.0f * kOver255, 5.0f};
inline constexpr LightOptions kQuestLight = {120.0f * kOver255, 72.0f * kOver255, 120.0f * kOver255, 5.0f};
inline constexpr float kFogNightStartDistance = 50.0f;
inline constexpr float kFogDayStartDistance = 90.0f;
inline constexpr int kLightingPeriod = 10000;        // milliseconds between blood stains' re-lighting
inline constexpr float kDefaultWindForce = 0.4f;     // the strongest wind a map may have
inline constexpr int kDefaultTimeScale = 60;         // the day and night clock: bigger is slower
inline constexpr const char* kDefaultMapName = "Zone1.mpr";
inline constexpr const char* kDefaultStreamingSound = "melody.mp3";

// The whole object; the settings file's value names in comments (a member named after its value is not commented).
// The sections: options.cpp's tables.
class GameOptions {
public:
    // What the static initialiser builds before WinMain: the defaults of a value the settings file lacks (the file's
    // own defaults: settings::Defaults; what each option does: src/game/settings_catalog.inc).
    GameOptions();
    // Reads every option from the settings file; a missing value keeps the current one (the defaults set before).
    void Load();
    // Writes the same values back and saves the file (WinMain calls it once, on exit). The camera flips are written
    // as booleans, the light colours as 0..255 and "Difficulty Level" from difficultyStored.
    void Save();

    std::string workPath;           // Work path
    std::string resourcesPath;      // Resources path
    int resolutionX;                 // VideoResolution
    int resolutionY;
    int fullScreen;                  // FullScreen
    int video;
    int audio;
    int audioStreaming;
    int audioSfx;
    int audioSpeech;
    float brightness;
    float gamma;
    int reverseStereo;               // "Reverse Stereo"
    int shadows;                     // Shadows: every shadow on or off
    float borderFogDistance;
    float cameraFlipX;               // CameraFlipXAxis: -1 or 1
    float cameraFlipY;
    float cameraMinHeight;
    float cameraMaxHeight;
    float cameraDefaultHeight;
    float cameraDefaultXDeploy;
    float cameraDefaultYDeploy;
    float cameraMinDistance;         // CameraMinDistanceToCarrier
    float cameraMaxDistance;
    float cameraMinLimitDistance;    // CameraMinLimitDistanceToCarrier
    float cameraMaxLimitDistance;
    float cameraDefaultDistance;     // CameraDefaultDistanceToCarrier
    float briefingMinDistance;       // CameraBriefingMinDistanceToCarrier
    float briefingMaxDistance;
    float briefingMinLimitDistance;
    float briefingMaxLimitDistance;
    float briefingMinPitch;          // CameraBriefingMinPitch
    float briefingMaxPitch;
    int cameraBorderScrollArea;
    int cameraFrameSelectionArea;    // CameraFrameSelectionSensetiveArea
    float cameraTerrainArea;         // CameraTerrainSensetiveArea
    CameraMotionOptions cameraScroll;
    CameraMotionOptions cameraRotate;
    CameraMotionOptions cameraPitch;
    CameraMotionOptions cameraZoom;
    CameraShortcutOptions cameraShortcuts[4];
    float cameraDefaultMinPitch;
    float cameraDefaultMaxPitch;
    float nearClipDistance;
    float farClipDistance;           // FarClipDistance
    int selectionType;
    int drawVisibleOnly;             // not a setting: 1 draws only the units the party sees (the spectator switch turns it off)
    int drawPath;
    int drawFlyingHp;                // DrawFlyingHP
    int showTutorial;
    int autosave;
    float toolTipTime;               // "ToolTip Time"
    int switchFilters;
    int difficulty;                  // "Difficulty Level"
    int difficultyStored;            // the same value: the one that is written back
    std::string masterServerName;   // "Master Server Name"
};
// The game's options (options_construct.cpp).
extern GameOptions g_options;

} // namespace options
