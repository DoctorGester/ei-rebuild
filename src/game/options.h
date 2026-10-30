#pragma once

// The game options object (a global), its loader from the settings file (settings.h; called once by WinMain) and
// the matching writer. Names are provisional; see docs/notes/objects.md ("Options").

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
// A light's colour (0..1; the settings file holds 0..255) and radius.
struct LightOptions {
    float red;
    float green;
    float blue;
    float radius;
};

// The whole object; the settings file's value names in comments (a member named after its value is not commented).
// The sections: options.cpp's tables.
class GameOptions {
public:
    // What the static initialiser builds before WinMain: the first defaults (800 x 600, 16 bits; the defaults
    // function sets the game's real defaults later, before the settings are read).
    GameOptions();
    // Reads every option from the settings file; a missing value keeps the current one (the defaults set before).
    void Load();
    // Writes the same values back and saves the file (WinMain calls it once, on exit). The camera flips are written
    // as booleans, the light colours as 0..255, "Joystick" always as false, and "Difficulty Level" from
    // difficultyStored.
    void Save();

    std::string workPath;           // Work path
    std::string resourcesPath;      // Resources path
    std::string cdRomPath;          // CD-ROM Path
    int driverIndex;                 // DrawingTarget: the Direct3D driver (-1: the first hardware one)
    int resolutionX;                 // VideoResolution
    int resolutionY;
    int windowWidth = 0, windowHeight = 0;   // configured window points, before the backing pixel size is known
    int colourDepth;                 // VideoColorDepthInBits
    int zBufferDepth;                // VideoZBufferDepthInBits
    int refreshRate;                 // VideoRefreshRate
    int fullScreen;                  // FullScreen
    int joystick;
    int video;
    int audio;
    int audioStreaming;
    int audioSfx;
    int audioSpeech;
    int dithering;                   // Dithering
    int specular;                    // Specular
    int antialiasing;                // Antialiasing
    int textureQuality;              // TextureQuality
    int mipmapping;                  // Mipmapping (cleared when the driver has no mipmaps)
    int filtering;                   // Filtering: 0 point, 1 / 2 linear
    int perspectiveCorrection;       // PerspectiveCorrection
    int showParticles;
    int fpsIndependentCursor;
    int automaticQuantization;
    int movieCapturingFps;
    float brightness;
    float contrast;
    float gamma;
    int reverseStereo;               // "Reverse Stereo"
    int shadowQuality;
    int heroShadow;
    int unitShadow;
    int buildingShadow;
    int floraShadow;
    int otherShadow;
    int shadowLightingFreq;          // LightingFreq of the shadow settings
    int floraSpecialLight;
    float borderFogDistance;
    int enableWaterWaves;
    int enableSelfShadowing;
    int enableDynamicLights;
    int lightingFreq;                // LightingFreq of the terrain settings: loaded and saved, read by nothing
    int shadowingFreq;
    int enableFootprints;
    int enableBloodprints;
    int enableFireprints;
    int enableBloodprintsRelight;
    int createOutputInfo;
    int numberOfFrames;
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
    float defaultWindForce;
    int defaultTimeScale;
    int useSwCursors;                // UseSWCursors
    int selectionType;
    float cornerSize;
    int drawVisibleOnly;
    int drawPath;
    int drawFlyingHp;                // DrawFlyingHP
    int showTutorial;
    int autosave;
    float toolTipTime;               // "ToolTip Time"
    int switchFilters;
    std::string cdLabels[3];        // "CD1 Label" ..
    std::string fontCharSet;        // "Font CharSet"
    int keyboardLayout;              // "Keyboard Layout"
    float objectsLightingCoeff;
    LightOptions heroLight;
    LightOptions questLight;
    int difficulty;                  // "Difficulty Level"
    int difficultyStored;            // the same value: the one that is written back
    std::string defaultMapName;
    std::string defaultStreamingSound;
    float fogNightStartDistance;
    float fogDayStartDistance;       // FogDayStartDistance
    std::string masterServerName;   // "Master Server Name"
};
// The game's options (options_construct.cpp).
extern GameOptions g_options;

// The options' defaults.
void OptionsResetDefaults(GameOptions* self);

} // namespace options
