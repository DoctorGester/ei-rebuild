// The options object's constructor (00458870; A-E2). The static initialiser 00458700 runs it on
// the global at 0079BC70 before WinMain (and registers the destructor 00458730 with atexit), so the
// original has already run when the hooks install; tests/verify/verify_static_initialisers.cpp
// builds a second object with this one at that point and compares the two.
//
// These are not the defaults the game plays with: 00455BA0 calls OptionsResetDefaults (00458DA0)
// before the registry is read, and that sets most of them again (640 x 480 and so on). What stays
// from here: the "Network Settings" subkey, the CD labels, the font's character set, the keyboard
// layout, both lights' radius and a few values 00458DA0 does not touch. The master server's name
// stays empty.
#include "options.h"

#include "ei/hook.h"

#include <cstdint>
#include <new>

namespace options {

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

} // namespace

// The eighteen strings start empty (the members' own constructors, in the object's order).
GameOptions::GameOptions() {
    keyStatistics_ = GameText(0x00784B20);   // "Statistics Output Settings"
    keyGame_ = GameText(0x00784B08);         // "Game Specific Settings"
    keyTerrain_ = GameText(0x00784AF4);      // "Terrain Settings"
    keyGeneral_ = GameText(0x00784AE0);      // "General Settings"
    keyCamera_ = GameText(0x00784AD0);       // "Camera Settings"
    keyShadows_ = GameText(0x00784AC0);      // "Shadow Settings"
    keyPaths_ = GameText(0x00784AB0);        // "Path Settings"
    keyNetwork_ = GameText(0x00784A9C);      // "Network Settings"

    driverIndex_ = 0;
    resolutionX_ = 800;
    resolutionY_ = 600;
    colourDepth_ = 16;
    zBufferDepth_ = 16;
    refreshRate_ = 0;
    fullScreen_ = 1;
    joystick_ = 0;
    video_ = 0;
    audio_ = 1;
    audioStreaming_ = 100;
    audioSfx_ = 100;
    audioSpeech_ = 100;
    antialiasing_ = 0;
    dithering_ = 0;
    specular_ = 0;
    textureQuality_ = 1;
    mipmapping_ = 0;
    filtering_ = 1;
    perspectiveCorrection_ = 1;
    shadowLightingFreq_ = 10000;
    floraSpecialLight_ = 0;
    showParticles_ = 1;
    fpsIndependentCursor_ = 1;
    automaticQuantization_ = 0;
    movieCapturingFps_ = 15;
    brightness_ = 0.0f;
    contrast_ = 0.0f;
    gamma_ = 0.0f;
    reverseStereo_ = 0;
    shadowQuality_ = 0;
    heroShadow_ = 1;
    unitShadow_ = 1;
    buildingShadow_ = 0;
    floraShadow_ = 0;
    otherShadow_ = 0;
    lod1_ = 40.0f;
    lod2_ = 75.0f;
    borderFogDistance_ = 32.0f;
    enableSelfShadowing_ = 0;
    enableWaterWaves_ = 1;
    enableDynamicLights_ = 1;
    lightingFreq_ = 1000;
    shadowingFreq_ = 6000;
    enableFootprints_ = 1;
    enableBloodprints_ = 1;
    enableFireprints_ = 1;
    enableBloodprintsRelight_ = 0;

    workPath_ = GameText(0x00784A98);        // ".\"
    resourcesPath_ = GameText(0x00784A90);   // ".\res\"
    cdRomPath_ = GameText(0x00784A98);

    createOutputInfo_ = 0;
    numberOfFrames_ = -1;
    cameraFlipX_ = 1.0f;
    cameraFlipY_ = 1.0f;
    cameraMinHeight_ = 4.0f;
    cameraMaxHeight_ = 100.0f;
    cameraDefaultHeight_ = 30.0f;
    cameraDefaultXDeploy_ = 0.0f;
    cameraDefaultYDeploy_ = 0.0f;
    cameraMinDistance_ = 4.0f;
    cameraMaxDistance_ = 100.0f;
    cameraMinLimitDistance_ = 3.0f;
    cameraMaxLimitDistance_ = 80.0f;
    cameraDefaultDistance_ = 30.0f;
    cameraDefaultMinPitch_ = 20.0f;
    cameraDefaultMaxPitch_ = 80.0f;
    briefingMinDistance_ = 4.0f;
    briefingMaxDistance_ = 100.0f;
    briefingMinLimitDistance_ = 3.0f;
    briefingMaxLimitDistance_ = 80.0f;
    briefingMinPitch_ = 30.0f;
    briefingMaxPitch_ = 60.0f;
    cameraBorderScrollArea_ = 5;
    cameraFrameSelectionArea_ = 20;
    cameraTerrainArea_ = 10.0f;
    cameraScroll_ = {1.0f, 1.0f, 0.5f};
    cameraRotate_ = {1.0f, 1.0f, 0.5f};
    cameraPitch_ = {1.0f, 1.0f, 0.5f};
    cameraZoom_ = {1.0f, 1.0f, 0.5f};
    // Every shortcut: the default distance, looking along Z, 45 degrees of pitch.
    for (CameraShortcutOptions& shortcut : cameraShortcuts_)
        shortcut = {cameraDefaultDistance_, 0.0f, 0.0f, 1.0f, 0.0f, 45.0f};

    nearClipDistance_ = 2.0f;
    farClipDistance_ = 100.0f;
    defaultWindForce_ = 0.4f;
    defaultTimeScale_ = 60;
    useSwCursors_ = 1;
    selectionType_ = 0;
    cornerSize_ = 0.5f;
    drawVisibleOnly_ = 1;
    drawPath_ = 1;
    drawFlyingHp_ = 1;
    showTutorial_ = 1;
    autosave_ = 1;
    toolTipTime_ = 0.5f;
    switchFilters_ = 1;

    cdLabels_[0] = GameText(0x00784A88);     // "EICD1"
    cdLabels_[1] = GameText(0x00784A80);     // "EICD2"
    cdLabels_[2] = GameText(0x00784A78);     // "EICD3"
    fontCharSet_ = GameText(0x00784A70);     // "english"

    keyboardLayout_ = 0x409;   // US English
    objectsLightingCoeff_ = 1.0f;
    heroLight_ = {1.0f, 0.925490201f, 0.666666687f, 10.0f};   // 255, 236, 170 of 255 (3F6CECED, 3F2AAAAB)
    questLight_ = {0.784313738f, 1.0f, 1.0f, 10.0f};          // 200 of 255 (3F48C8C9)
    difficulty_ = 0;
    difficultyStored_ = 0;

    defaultMapName_ = GameText(0x007846F0);          // "none"
    defaultStreamingSound_ = GameText(0x007846F0);
    fogNightStartDistance_ = 50.0f;
    fogDayStartDistance_ = 90.0f;
}

// 00458870 (thiscall; returns the object).
GameOptions* __fastcall OptionsConstruct(GameOptions* self) { return new (self) GameOptions; }
EI_REPLACE(0x00458870, OptionsConstruct);

} // namespace options
