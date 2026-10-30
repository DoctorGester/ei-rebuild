// The options object's constructor.
// More: docs/notes/comments-c22i.md, "options.cpp".
#include "options.h"

#include <cstdint>

namespace options {

// The eighteen strings start empty (the members' own constructors, in the object's order).
GameOptions::GameOptions() {
    keyStatistics = "Statistics Output Settings";
    keyGame = "Game Specific Settings";
    keyTerrain = "Terrain Settings";
    keyGeneral = "General Settings";
    keyCamera = "Camera Settings";
    keyShadows = "Shadow Settings";
    keyPaths = "Path Settings";
    keyNetwork = "Network Settings";

    driverIndex = 0;
    resolutionX = 800;
    resolutionY = 600;
    colourDepth = 16;
    zBufferDepth = 16;
    refreshRate = 0;
    fullScreen = 1;
    joystick = 0;
    video = 0;
    audio = 1;
    audioStreaming = 100;
    audioSfx = 100;
    audioSpeech = 100;
    antialiasing = 0;
    dithering = 0;
    specular = 0;
    textureQuality = 1;
    mipmapping = 0;
    filtering = 1;
    perspectiveCorrection = 1;
    shadowLightingFreq = 10000;
    floraSpecialLight = 0;
    showParticles = 1;
    fpsIndependentCursor = 1;
    automaticQuantization = 0;
    movieCapturingFps = 15;
    brightness = 0.0f;
    contrast = 0.0f;
    gamma = 0.0f;
    reverseStereo = 0;
    shadowQuality = 0;
    heroShadow = 1;
    unitShadow = 1;
    buildingShadow = 0;
    floraShadow = 0;
    otherShadow = 0;
    lod1 = 40.0f;
    lod2 = 75.0f;
    borderFogDistance = 32.0f;
    enableSelfShadowing = 0;
    enableWaterWaves = 1;
    enableDynamicLights = 1;
    lightingFreq = 1000;
    shadowingFreq = 6000;
    enableFootprints = 1;
    enableBloodprints = 1;
    enableFireprints = 1;
    enableBloodprintsRelight = 0;

    workPath = ".\\";
    resourcesPath = ".\\res\\";
    cdRomPath = ".\\";

    createOutputInfo = 0;
    numberOfFrames = -1;
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
    useSwCursors = 1;
    selectionType = 0;
    cornerSize = 0.5f;
    drawVisibleOnly = 1;
    drawPath = 1;
    drawFlyingHp = 1;
    showTutorial = 1;
    autosave = 1;
    toolTipTime = 0.5f;
    switchFilters = 1;

    cdLabels[0] = "EICD1";
    cdLabels[1] = "EICD2";
    cdLabels[2] = "EICD3";
    fontCharSet = "english";

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

// The game's options: constructed by the C++ runtime before the static initialisers and WinMain (LoadFromRegistry
// fills them), destroyed after the game's exit functions (ShutDown's SaveToRegistry has written them back).
GameOptions g_options;

} // namespace options

// --- Options defaults --------------------------------------------------------------------------------

// The options' defaults (called before the registry is read): the subkey names, the video mode (640 x 480, 16
// bits), the camera, shadows and lights, the paths ("." and ".\res\"), the map and stream names and the
// master server ("a3master.nival.com"). See docs/notes/objects.md ("Rewritten").

namespace options {

void OptionsResetDefaults(GameOptions* self) {
    self->keyStatistics = "Statistics Output Settings";
    self->keyGame = "Game Specific Settings";
    self->keyTerrain = "Terrain Settings";
    self->keyGeneral = "General Settings";
    self->keyCamera = "Camera Settings";
    self->keyShadows = "Shadow Settings";
    self->keyPaths = "Path Settings";
    self->driverIndex = 0;
    self->resolutionX = 0x280;
    self->resolutionY = 0x1E0;
    self->colourDepth = 16;
    self->zBufferDepth = 16;
    self->refreshRate = 0;
    self->fullScreen = 1;
    self->joystick = 0;
    self->video = 0;
    self->audio = 0;
    self->audioStreaming = 0;
    self->audioSfx = 0;
    self->audioSpeech = 0;
    self->antialiasing = 1;
    self->dithering = 0;
    self->specular = 1;
    self->textureQuality = 3;
    self->mipmapping = 0;
    self->filtering = 0;
    self->perspectiveCorrection = 0;
    self->shadowLightingFreq = 1;
    self->floraSpecialLight = 1;
    self->showParticles = 0;
    self->fpsIndependentCursor = 1;
    self->automaticQuantization = 1;
    self->movieCapturingFps = 15;
    self->brightness = 1.0f;
    self->contrast = 1.0f;
    self->gamma = 1.0f;
    self->shadowQuality = 0;
    self->heroShadow = 0;
    self->unitShadow = 0;
    self->buildingShadow = 0;
    self->floraShadow = 0;
    self->otherShadow = 0;
    self->lod1 = 33.0f;
    self->lod2 = 66.0f;
    self->borderFogDistance = 2.0f;
    self->enableSelfShadowing = 1;
    self->enableWaterWaves = 1;
    self->enableDynamicLights = 1;
    self->lightingFreq = 1;
    self->shadowingFreq = 1;
    self->enableFootprints = 1;
    self->enableBloodprints = 1;
    self->enableFireprints = 1;
    self->workPath = ".\\";
    self->resourcesPath = ".\\res\\";
    self->cdRomPath = ".\\";
    self->createOutputInfo = 0;
    self->numberOfFrames = -1;
    self->cameraMinHeight = -100.0f;
    self->cameraMaxHeight = 1000.0f;
    self->cameraDefaultHeight = 30.0f;
    self->cameraDefaultXDeploy = 0.0f;
    self->cameraDefaultYDeploy = 0.0f;
    self->cameraMinDistance = -100.0f;
    self->cameraMaxDistance = 100.0f;
    self->cameraMinLimitDistance = -50.0f;
    self->cameraMaxLimitDistance = 80.0f;
    self->cameraDefaultDistance = 30.0f;
    self->cameraDefaultMinPitch = 0.0f;
    self->cameraDefaultMaxPitch = 90.0f;
    self->briefingMinDistance = 4.0f;
    self->briefingMaxDistance = 100.0f;
    self->briefingMinLimitDistance = 3.0f;
    self->briefingMaxLimitDistance = 80.0f;
    self->briefingMinPitch = 30.0f;
    self->briefingMaxPitch = 60.0f;
    self->cameraBorderScrollArea = 200;
    self->cameraFrameSelectionArea = 1;
    self->cameraTerrainArea = 1.0f;
    self->cameraScroll.speed = 1.0f;
    self->cameraScroll.powerMouse = 100.0f;
    self->cameraScroll.powerKeyboard = 0.5f;
    self->cameraRotate.speed = 1.0f;
    self->cameraRotate.powerMouse = 100.0f;
    self->cameraRotate.powerKeyboard = 0.5f;
    self->cameraPitch.speed = 1.0f;
    self->cameraPitch.powerMouse = 100.0f;
    self->cameraPitch.powerKeyboard = 0.5f;
    self->cameraZoom.speed = 1.0f;
    self->cameraZoom.powerMouse = 100.0f;
    self->cameraZoom.powerKeyboard = 0.5f;
    self->nearClipDistance = 2.0f;
    self->farClipDistance = 100.0f;
    self->defaultWindForce = 0.4f;
    self->defaultTimeScale = 6;
    self->useSwCursors = 1;
    self->selectionType = 0;
    self->cornerSize = 0.5f;
    self->drawVisibleOnly = 1;
    self->drawPath = 0;
    self->drawFlyingHp = 0;
    self->objectsLightingCoeff = 0.0f;
    self->heroLight.red = 1.0f;
    self->heroLight.green = 0.9254902f;
    self->heroLight.blue = 0.6666667f;
    self->questLight.red = 0.78431374f;
    self->questLight.green = 1.0f;
    self->questLight.blue = 1.0f;
    self->defaultMapName = "none";
    self->defaultStreamingSound = "none";
    self->fogNightStartDistance = 50.0f;
    self->fogDayStartDistance = 90.0f;
    self->masterServerName = "a3master.nival.com";
}

} // namespace options
