// The options' defaults (00458DA0; called by 00455BA0 before the registry is read): the subkey
// names, the video mode (640 x 480, 16 bits), the camera, shadows and lights, the paths ("." and
// ".\res\"), the map and stream names and the master server ("a3master.nival.com"). See
// docs/notes/objects.md ("Rewritten").
#include "options.h"

#include "ei/hook.h"

namespace options {

void __fastcall OptionsResetDefaults(GameOptions* self) {
    self->keyStatistics_ = "Statistics Output Settings";
    self->keyGame_ = "Game Specific Settings";
    self->keyTerrain_ = "Terrain Settings";
    self->keyGeneral_ = "General Settings";
    self->keyCamera_ = "Camera Settings";
    self->keyShadows_ = "Shadow Settings";
    self->keyPaths_ = "Path Settings";
    self->driverIndex_ = 0;
    self->resolutionX_ = 0x280;
    self->resolutionY_ = 0x1E0;
    self->colourDepth_ = 16;
    self->zBufferDepth_ = 16;
    self->refreshRate_ = 0;
    self->fullScreen_ = 1;
    self->joystick_ = 0;
    self->video_ = 0;
    self->audio_ = 0;
    self->audioStreaming_ = 0;
    self->audioSfx_ = 0;
    self->audioSpeech_ = 0;
    self->antialiasing_ = 1;
    self->dithering_ = 0;
    self->specular_ = 1;
    self->textureQuality_ = 3;
    self->mipmapping_ = 0;
    self->filtering_ = 0;
    self->perspectiveCorrection_ = 0;
    self->shadowLightingFreq_ = 1;
    self->floraSpecialLight_ = 1;
    self->showParticles_ = 0;
    self->fpsIndependentCursor_ = 1;
    self->automaticQuantization_ = 1;
    self->movieCapturingFps_ = 15;
    self->brightness_ = 1.0f;
    self->contrast_ = 1.0f;
    self->gamma_ = 1.0f;
    self->shadowQuality_ = 0;
    self->heroShadow_ = 0;
    self->unitShadow_ = 0;
    self->buildingShadow_ = 0;
    self->floraShadow_ = 0;
    self->otherShadow_ = 0;
    self->lod1_ = 33.0f;
    self->lod2_ = 66.0f;
    self->borderFogDistance_ = 2.0f;
    self->enableSelfShadowing_ = 1;
    self->enableWaterWaves_ = 1;
    self->enableDynamicLights_ = 1;
    self->lightingFreq_ = 1;
    self->shadowingFreq_ = 1;
    self->enableFootprints_ = 1;
    self->enableBloodprints_ = 1;
    self->enableFireprints_ = 1;
    self->workPath_ = ".\\";
    self->resourcesPath_ = ".\\res\\";
    self->cdRomPath_ = ".\\";
    self->createOutputInfo_ = 0;
    self->numberOfFrames_ = -1;
    self->cameraMinHeight_ = -100.0f;
    self->cameraMaxHeight_ = 1000.0f;
    self->cameraDefaultHeight_ = 30.0f;
    self->cameraDefaultXDeploy_ = 0.0f;
    self->cameraDefaultYDeploy_ = 0.0f;
    self->cameraMinDistance_ = -100.0f;
    self->cameraMaxDistance_ = 100.0f;
    self->cameraMinLimitDistance_ = -50.0f;
    self->cameraMaxLimitDistance_ = 80.0f;
    self->cameraDefaultDistance_ = 30.0f;
    self->cameraDefaultMinPitch_ = 0.0f;
    self->cameraDefaultMaxPitch_ = 90.0f;
    self->briefingMinDistance_ = 4.0f;
    self->briefingMaxDistance_ = 100.0f;
    self->briefingMinLimitDistance_ = 3.0f;
    self->briefingMaxLimitDistance_ = 80.0f;
    self->briefingMinPitch_ = 30.0f;
    self->briefingMaxPitch_ = 60.0f;
    self->cameraBorderScrollArea_ = 200;
    self->cameraFrameSelectionArea_ = 1;
    self->cameraTerrainArea_ = 1.0f;
    self->cameraScroll_.speed = 1.0f;
    self->cameraScroll_.powerMouse = 100.0f;
    self->cameraScroll_.powerKeyboard = 0.5f;
    self->cameraRotate_.speed = 1.0f;
    self->cameraRotate_.powerMouse = 100.0f;
    self->cameraRotate_.powerKeyboard = 0.5f;
    self->cameraPitch_.speed = 1.0f;
    self->cameraPitch_.powerMouse = 100.0f;
    self->cameraPitch_.powerKeyboard = 0.5f;
    self->cameraZoom_.speed = 1.0f;
    self->cameraZoom_.powerMouse = 100.0f;
    self->cameraZoom_.powerKeyboard = 0.5f;
    self->nearClipDistance_ = 2.0f;
    self->farClipDistance_ = 100.0f;
    self->defaultWindForce_ = 0.4f;
    self->defaultTimeScale_ = 6;
    self->useSwCursors_ = 1;
    self->selectionType_ = 0;
    self->cornerSize_ = 0.5f;
    self->drawVisibleOnly_ = 1;
    self->drawPath_ = 0;
    self->drawFlyingHp_ = 0;
    self->objectsLightingCoeff_ = 0.0f;
    self->heroLight_.red = 1.0f;
    self->heroLight_.green = 0.9254902f;
    self->heroLight_.blue = 0.6666667f;
    self->questLight_.red = 0.78431374f;
    self->questLight_.green = 1.0f;
    self->questLight_.blue = 1.0f;
    self->defaultMapName_ = "none";
    self->defaultStreamingSound_ = "none";
    self->fogNightStartDistance_ = 50.0f;
    self->fogDayStartDistance_ = 90.0f;
    self->masterServerName_ = "a3master.nival.com";
}
EI_REWRITTEN(0x00458DA0, OptionsResetDefaults);

// 00458730: the options object's destructor (at exit, registered by the static initialiser
// 00458700): its eighteen CStrings, the last first (the members' destruction order).
void __fastcall OptionsDestruct(GameOptions* self) { self->~GameOptions(); }
EI_REPLACE(0x00458730, OptionsDestruct);

} // namespace options
