// The options object's constructor.
// More: docs/notes/comments-c22i.md, "options.cpp".
#include "options.h"

#include <cstdint>

namespace options {

// The strings start empty (the members' own constructors).
GameOptions::GameOptions() {
    driverIndex = 0;
    resolutionX = 800;
    resolutionY = 600;
    colourDepth = 16;
    fullScreen = 1;
    video = 0;
    audio = 1;
    audioStreaming = 100;
    audioSfx = 100;
    audioSpeech = 100;
    specular = 0;
    textureQuality = 1;
    mipmapping = 0;
    filtering = 1;
    shadowLightingFreq = 10000;
    floraSpecialLight = 0;
    fpsIndependentCursor = 1;
    brightness = 0.0f;
    contrast = 0.0f;
    gamma = 0.0f;
    reverseStereo = 0;
    shadowQuality = 0;
    unitShadow = 1;
    buildingShadow = 0;
    floraShadow = 0;
    borderFogDistance = 16.0f;   // the installed game's value (the original constructor had 32, which fogs the menu map over)
    enableSelfShadowing = 0;
    enableWaterWaves = 1;
    enableFootprints = 1;
    enableBloodprints = 1;
    enableFireprints = 1;
    enableBloodprintsRelight = 0;

    workPath = ".\\";
    resourcesPath = ".\\res\\";
    cdRomPath = ".\\";

    cameraFlipX = 1.0f;
    cameraFlipY = 1.0f;
    cameraMinHeight = 4.0f;
    cameraMaxHeight = 100.0f;
    cameraDefaultHeight = 30.0f;
    cameraDefaultXDeploy = 0.0f;
    cameraDefaultYDeploy = 0.0f;
    cameraMinDistance = 4.0f;
    cameraMaxDistance = 100.0f;
    cameraMinLimitDistance = 3.0f;
    cameraMaxLimitDistance = 80.0f;
    cameraDefaultDistance = 30.0f;
    cameraDefaultMinPitch = 20.0f;
    cameraDefaultMaxPitch = 80.0f;
    briefingMinDistance = 4.0f;
    briefingMaxDistance = 100.0f;
    briefingMinLimitDistance = 3.0f;
    briefingMaxLimitDistance = 80.0f;
    briefingMinPitch = 30.0f;
    briefingMaxPitch = 60.0f;
    cameraBorderScrollArea = 5;
    cameraFrameSelectionArea = 20;
    cameraTerrainArea = 10.0f;
    cameraScroll = {1.0f, 1.0f, 0.5f};
    cameraRotate = {1.0f, 1.0f, 0.5f};
    cameraPitch = {1.0f, 1.0f, 0.5f};
    cameraZoom = {1.0f, 1.0f, 0.5f};
    // Every shortcut: the default distance, looking along Z, 45 degrees of pitch.
    for (CameraShortcutOptions& shortcut : cameraShortcuts)
        shortcut = {cameraDefaultDistance, 0.0f, 0.0f, 1.0f, 0.0f, 45.0f};

    nearClipDistance = 2.0f;
    farClipDistance = 100.0f;
    defaultWindForce = 0.4f;
    defaultTimeScale = 60;
    selectionType = 0;
    drawVisibleOnly = 1;
    drawPath = 1;
    drawFlyingHp = 1;
    showTutorial = 1;
    autosave = 1;
    toolTipTime = 0.5f;
    switchFilters = 1;

    cd2Label = "EICD2";

    keyboardLayout = 0x409;    // US English
    objectsLightingCoeff = 1.0f;
    heroLight = {1.0f, 0.925490201f, 0.666666687f, 10.0f};    // 255, 236, 170 of 255
    questLight = {0.784313738f, 1.0f, 1.0f, 10.0f};           // 200 of 255
    difficulty = 0;
    difficultyStored = 0;

    defaultMapName = "none";
    defaultStreamingSound = "none";
    fogNightStartDistance = 50.0f;
    fogDayStartDistance = 90.0f;
}

// The game's options: constructed by the C++ runtime before the static initialisers and WinMain (Load fills them),
// destroyed after the game's exit functions (ShutDown's Save has written them back).
GameOptions g_options;

} // namespace options
