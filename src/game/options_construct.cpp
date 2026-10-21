// The options object's constructor (00458870; A-E2). The static initialiser 00458700 runs it on
// the global at 0079BC70 before WinMain (and registers the destructor 00458730 with atexit), so the
// original has already run when the hooks install; verify_static_initialisers.cpp built a second
// object with this one at that point and compared the two (until S21d).
//
// These are not the defaults the game plays with: 00455BA0 calls OptionsResetDefaults (00458DA0)
// before the registry is read, and that sets most of them again (640 x 480 and so on). What stays
// from here: the "Network Settings" subkey, the CD labels, the font's character set, the keyboard
// layout, both lights' radius and a few values 00458DA0 does not touch. The master server's name
// stays empty.
#include "options.h"
#include "game_strings.h"
#include "originals_library.h"

#include "ei/hook.h"

#include <cstdint>
#include <new>

namespace options {

namespace {


} // namespace

// The eighteen strings start empty (the members' own constructors, in the object's order).
GameOptions::GameOptions() {
    keyStatistics_ = "Statistics Output Settings";
    keyGame_ = "Game Specific Settings";
    keyTerrain_ = "Terrain Settings";
    keyGeneral_ = "General Settings";
    keyCamera_ = "Camera Settings";
    keyShadows_ = "Shadow Settings";
    keyPaths_ = "Path Settings";
    keyNetwork_ = "Network Settings";

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

    workPath_ = ".\\";
    resourcesPath_ = ".\\res\\";
    cdRomPath_ = ".\\";

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

    cdLabels_[0] = "EICD1";
    cdLabels_[1] = "EICD2";
    cdLabels_[2] = "EICD3";
    fontCharSet_ = "english";

    keyboardLayout_ = 0x409;   // US English
    objectsLightingCoeff_ = 1.0f;
    heroLight_ = {1.0f, 0.925490201f, 0.666666687f, 10.0f};   // 255, 236, 170 of 255 (3F6CECED, 3F2AAAAB)
    questLight_ = {0.784313738f, 1.0f, 1.0f, 10.0f};          // 200 of 255 (3F48C8C9)
    difficulty_ = 0;
    difficultyStored_ = 0;

    defaultMapName_ = "none";
    defaultStreamingSound_ = "none";
    fogNightStartDistance_ = 50.0f;
    fogDayStartDistance_ = 90.0f;
}

// 00458870 (thiscall; returns the object).
GameOptions* __fastcall OptionsConstruct(GameOptions* self) { return new (self) GameOptions; }
EI_REPLACE(0x00458870, OptionsConstruct);

// The static initialiser 00458700 and the exit function 00458720 it registers, ported on the name (g_options,
// options_global.cpp) and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects").
namespace {
void __cdecl ConstructGameOptions() {   // 00458700
    g_options.Construct();
    GameAtExit(ei::Code<void(__cdecl*)()>(0x00458720));
}
void __cdecl DestroyGameOptions() { g_options.Destroy(); }   // 00458720
} // namespace
EI_REPLACE(0x00458700, ConstructGameOptions);
EI_REPLACE(0x00458720, DestroyGameOptions);

} // namespace options
