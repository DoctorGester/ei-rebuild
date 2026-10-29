// The options loader and writer. See options.h.
// Every value is read through the game's registry key helper, in the game's order; a value
// that is missing keeps the field's current value, which is passed as the default.
#include "options.h"

#include "win_registry.h"

#include "lib/crt.h"
#include "lib/str.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <format>

namespace options {
namespace {

constexpr float kTimes255 = 255.0f;
constexpr float kOver255 = 1.0f / 255.0f;     // (0x3B808081)
constexpr std::uint32_t kMinusOneBits = 0xBF800000u;   // -1.0f

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// _ftol(value * 255): the product rounded only by the FPU's precision, then truncated by the
// game's _ftol.
int FtolTimes255(float value) {
    return static_cast<int>(libcrt::Ftol(static_cast<double>(value) * kTimes255));
}
// _ftol(value).
int Ftol(float value) { return static_cast<int>(libcrt::Ftol(value)); }
// `value` as an unsigned 64-bit integer times factor: the product rounded
// by the FPU's precision, then once to float.
float UnsignedProduct(std::uint32_t value, float factor) { return static_cast<float>(x87::FromUnsigned(value) * factor); }
// `value` as an unsigned 64-bit integer, as a float.
float Unsigned(std::uint32_t value) { return static_cast<float>(x87::FromUnsigned(value)); }

// One value: the subkey field, the value name, the field it goes to (and whose value is the default): `number`
// for an integer (GetInt / SetInt), `real` for a float. Kept: a pointer-to-member type.
using KeyField = std::string GameOptions::*;
struct Value {
    constexpr Value(KeyField subkey, const char* valueName, int GameOptions::*field)
        : key(subkey), name(valueName), number(field) {}
    constexpr Value(KeyField subkey, const char* valueName, float GameOptions::*field)
        : key(subkey), name(valueName), real(field) {}
    KeyField key;
    const char* name;
    int GameOptions::*number = nullptr;
    float GameOptions::*real = nullptr;
};

constexpr Value kVideoValues[] = {
    {&GameOptions::keyGeneral_, "VideoColorDepthInBits", &GameOptions::colourDepth_},
    {&GameOptions::keyGeneral_, "VideoZBufferDepthInBits", &GameOptions::zBufferDepth_},
    {&GameOptions::keyGeneral_, "VideoRefreshRate", &GameOptions::refreshRate_},
    {&GameOptions::keyGeneral_, "FullScreen", &GameOptions::fullScreen_},
};
// Joystick (joystick_, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {&GameOptions::keyGeneral_, "Video", &GameOptions::video_},
    {&GameOptions::keyGeneral_, "Audio", &GameOptions::audio_},
    {&GameOptions::keyGeneral_, "AudioStreaming", &GameOptions::audioStreaming_},
    {&GameOptions::keyGeneral_, "AudioSFX", &GameOptions::audioSfx_},
    {&GameOptions::keyGeneral_, "AudioSpeech", &GameOptions::audioSpeech_},
    {&GameOptions::keyGeneral_, "Antialiasing", &GameOptions::antialiasing_},
    {&GameOptions::keyGeneral_, "Dithering", &GameOptions::dithering_},
    {&GameOptions::keyGeneral_, "Specular", &GameOptions::specular_},
    {&GameOptions::keyGeneral_, "TextureQuality", &GameOptions::textureQuality_},
    {&GameOptions::keyGeneral_, "Mipmapping", &GameOptions::mipmapping_},
    {&GameOptions::keyGeneral_, "Filtering", &GameOptions::filtering_},
    {&GameOptions::keyGeneral_, "PerspectiveCorrection", &GameOptions::perspectiveCorrection_},
    {&GameOptions::keyGeneral_, "ShowParticles", &GameOptions::showParticles_},
    {&GameOptions::keyGeneral_, "FPSIndependentCursor", &GameOptions::fpsIndependentCursor_},
    {&GameOptions::keyGeneral_, "AutomaticQuantization", &GameOptions::automaticQuantization_},
    {&GameOptions::keyGeneral_, "MovieCapturingFPS", &GameOptions::movieCapturingFps_},
    {&GameOptions::keyGeneral_, "Brightness", &GameOptions::brightness_},
    {&GameOptions::keyGeneral_, "Contrast", &GameOptions::contrast_},
    {&GameOptions::keyGeneral_, "Gamma", &GameOptions::gamma_},
    {&GameOptions::keyGeneral_, "Reverse Stereo", &GameOptions::reverseStereo_},
    {&GameOptions::keyShadows_, "ShadowQuality", &GameOptions::shadowQuality_},
    {&GameOptions::keyShadows_, "HeroShadow", &GameOptions::heroShadow_},
    {&GameOptions::keyShadows_, "UnitShadow", &GameOptions::unitShadow_},
    {&GameOptions::keyShadows_, "BuildingShadow", &GameOptions::buildingShadow_},
    {&GameOptions::keyShadows_, "FloraShadow", &GameOptions::floraShadow_},
    {&GameOptions::keyShadows_, "OtherShadow", &GameOptions::otherShadow_},
    {&GameOptions::keyShadows_, "LightingFreq", &GameOptions::shadowLightingFreq_},
    {&GameOptions::keyShadows_, "FloraSpecialLight", &GameOptions::floraSpecialLight_},
    {&GameOptions::keyTerrain_, "LOD1", &GameOptions::lod1_},
    {&GameOptions::keyTerrain_, "LOD2", &GameOptions::lod2_},
    {&GameOptions::keyTerrain_, "BorderFogDistance", &GameOptions::borderFogDistance_},
    {&GameOptions::keyTerrain_, "EnableWaterWaves", &GameOptions::enableWaterWaves_},
    {&GameOptions::keyTerrain_, "EnableDynamicLights", &GameOptions::enableDynamicLights_},
    {&GameOptions::keyTerrain_, "EnableSelfShadowing", &GameOptions::enableSelfShadowing_},
    {&GameOptions::keyTerrain_, "LightingFreq", &GameOptions::lightingFreq_},
    {&GameOptions::keyTerrain_, "ShadowingFreq", &GameOptions::shadowingFreq_},
    {&GameOptions::keyTerrain_, "EnableFootprints", &GameOptions::enableFootprints_},
    {&GameOptions::keyTerrain_, "EnableBloodprints", &GameOptions::enableBloodprints_},
    {&GameOptions::keyTerrain_, "EnableFireprints", &GameOptions::enableFireprints_},
    {&GameOptions::keyTerrain_, "EnableBloodprintsRelight", &GameOptions::enableBloodprintsRelight_},
    {&GameOptions::keyStatistics_, "CreateOutputInfo", &GameOptions::createOutputInfo_},
    {&GameOptions::keyStatistics_, "NumberOfFrames", &GameOptions::numberOfFrames_},
};
constexpr Value kCameraValues[] = {
    {&GameOptions::keyCamera_, "CameraMinHeight", &GameOptions::cameraMinHeight_},
    {&GameOptions::keyCamera_, "CameraMaxHeight", &GameOptions::cameraMaxHeight_},
    {&GameOptions::keyCamera_, "CameraDefaultHeight", &GameOptions::cameraDefaultHeight_},
    {&GameOptions::keyCamera_, "CameraDefaultXDeploy", &GameOptions::cameraDefaultXDeploy_},
    {&GameOptions::keyCamera_, "CameraDefaultYDeploy", &GameOptions::cameraDefaultYDeploy_},
    {&GameOptions::keyCamera_, "CameraMinDistanceToCarrier", &GameOptions::cameraMinDistance_},
    {&GameOptions::keyCamera_, "CameraMaxDistanceToCarrier", &GameOptions::cameraMaxDistance_},
    {&GameOptions::keyCamera_, "CameraMinLimitDistanceToCarrier", &GameOptions::cameraMinLimitDistance_},
    {&GameOptions::keyCamera_, "CameraMaxLimitDistanceToCarrier", &GameOptions::cameraMaxLimitDistance_},
    {&GameOptions::keyCamera_, "CameraDefaultDistanceToCarrier", &GameOptions::cameraDefaultDistance_},
    {&GameOptions::keyCamera_, "NearClipDistance", &GameOptions::nearClipDistance_},
    {&GameOptions::keyCamera_, "FarClipDistance", &GameOptions::farClipDistance_},
    {&GameOptions::keyCamera_, "CameraDefaultMinPitch", &GameOptions::cameraDefaultMinPitch_},
    {&GameOptions::keyCamera_, "CameraDefaultMaxPitch", &GameOptions::cameraDefaultMaxPitch_},
    {&GameOptions::keyCamera_, "CameraBriefingMinDistanceToCarrier", &GameOptions::briefingMinDistance_},
    {&GameOptions::keyCamera_, "CameraBriefingMaxDistanceToCarrier", &GameOptions::briefingMaxDistance_},
    {&GameOptions::keyCamera_, "CameraBriefingMinLimitDistanceToCarrier", &GameOptions::briefingMinLimitDistance_},
    {&GameOptions::keyCamera_, "CameraBriefingMaxLimitDistanceToCarrier", &GameOptions::briefingMaxLimitDistance_},
    {&GameOptions::keyCamera_, "CameraBriefingMinPitch", &GameOptions::briefingMinPitch_},
    {&GameOptions::keyCamera_, "CameraBriefingMaxPitch", &GameOptions::briefingMaxPitch_},
    {&GameOptions::keyCamera_, "CameraBorderScrollArea", &GameOptions::cameraBorderScrollArea_},
    {&GameOptions::keyCamera_, "CameraFrameSelectionSensetiveArea", &GameOptions::cameraFrameSelectionArea_},
    {&GameOptions::keyCamera_, "CameraTerrainSensetiveArea", &GameOptions::cameraTerrainArea_},
};
// Read right after kCameraValues: "CameraDefault<Scroll|Rotate|Pitch|Zoom><Speed|PowerMouse|PowerKeyboard>".
constexpr struct {
    const char* name;
    CameraMotionOptions GameOptions::*motion;
    float CameraMotionOptions::*part;
} kCameraMotionValues[] = {
    {"CameraDefaultScrollSpeed", &GameOptions::cameraScroll_, &CameraMotionOptions::speed},
    {"CameraDefaultScrollPowerMouse", &GameOptions::cameraScroll_, &CameraMotionOptions::powerMouse},
    {"CameraDefaultScrollPowerKeyboard", &GameOptions::cameraScroll_, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultRotateSpeed", &GameOptions::cameraRotate_, &CameraMotionOptions::speed},
    {"CameraDefaultRotatePowerMouse", &GameOptions::cameraRotate_, &CameraMotionOptions::powerMouse},
    {"CameraDefaultRotatePowerKeyboard", &GameOptions::cameraRotate_, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultPitchSpeed", &GameOptions::cameraPitch_, &CameraMotionOptions::speed},
    {"CameraDefaultPitchPowerMouse", &GameOptions::cameraPitch_, &CameraMotionOptions::powerMouse},
    {"CameraDefaultPitchPowerKeyboard", &GameOptions::cameraPitch_, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultZoomSpeed", &GameOptions::cameraZoom_, &CameraMotionOptions::speed},
    {"CameraDefaultZoomPowerMouse", &GameOptions::cameraZoom_, &CameraMotionOptions::powerMouse},
    {"CameraDefaultZoomPowerKeyboard", &GameOptions::cameraZoom_, &CameraMotionOptions::powerKeyboard},
};
constexpr Value kGameValues[] = {
    {&GameOptions::keyGame_, "DefaultWindForce", &GameOptions::defaultWindForce_},
    {&GameOptions::keyGame_, "DefaultTimeScale", &GameOptions::defaultTimeScale_},
    {&GameOptions::keyGame_, "UseSWCursors", &GameOptions::useSwCursors_},
    {&GameOptions::keyGame_, "SelectionType", &GameOptions::selectionType_},
    {&GameOptions::keyGame_, "CornerSize", &GameOptions::cornerSize_},
    {&GameOptions::keyGame_, "DrawVisibleOnly", &GameOptions::drawVisibleOnly_},
    {&GameOptions::keyGame_, "DrawPath", &GameOptions::drawPath_},
    {&GameOptions::keyGame_, "DrawFlyingHP", &GameOptions::drawFlyingHp_},
    {&GameOptions::keyGame_, "ShowTutorial", &GameOptions::showTutorial_},
    {&GameOptions::keyGame_, "Autosave", &GameOptions::autosave_},
    {&GameOptions::keyGame_, "ToolTip Time", &GameOptions::toolTipTime_},
    {&GameOptions::keyGame_, "SwitchFilters", &GameOptions::switchFilters_},
};
constexpr Value kGameValues2[] = {
    {&GameOptions::keyGame_, "Keyboard Layout", &GameOptions::keyboardLayout_},
    {&GameOptions::keyGame_, "ObjectsLightingCoeff", &GameOptions::objectsLightingCoeff_},
};
constexpr Value kFogValues[] = {
    {&GameOptions::keyGame_, "FogNightStartDistance", &GameOptions::fogNightStartDistance_},
    {&GameOptions::keyGame_, "FogDayStartDistance", &GameOptions::fogDayStartDistance_},
};

// One string option: subkey field, value name, field.
using StringField = std::string& (*)(GameOptions&);
template <std::string GameOptions::*Member>
std::string& StringOf(GameOptions& options) { return options.*Member; }
template <int Index>
std::string& CdLabel(GameOptions& options) { return options.cdLabels_[Index]; }
struct StringValue {
    KeyField key;
    const char* name;
    StringField field;
};
constexpr StringValue kPathValues[] = {
    {&GameOptions::keyPaths_, "Work path", &StringOf<&GameOptions::workPath_>},
    {&GameOptions::keyPaths_, "Resources path", &StringOf<&GameOptions::resourcesPath_>},
    {&GameOptions::keyPaths_, "CD-ROM Path", &StringOf<&GameOptions::cdRomPath_>},
};
constexpr StringValue kCdValues[] = {
    {&GameOptions::keyGame_, "CD1 Label", &CdLabel<0>},
    {&GameOptions::keyGame_, "CD2 Label", &CdLabel<1>},
    {&GameOptions::keyGame_, "CD3 Label", &CdLabel<2>},
    {&GameOptions::keyGame_, "Font CharSet", &StringOf<&GameOptions::fontCharSet_>},
};
constexpr StringValue kDefaultValues[] = {
    {&GameOptions::keyGame_, "DefaultMapName", &StringOf<&GameOptions::defaultMapName_>},
    {&GameOptions::keyGame_, "DefaultStreamingSound", &StringOf<&GameOptions::defaultStreamingSound_>},
};

// The lights: r, g, b stored as 0..1 (the registry holds 0..255), then the radius.
constexpr const char* kHeroLightNames[] = {"Hero Light R", "Hero Light G", "Hero Light B", "Hero Light Radius"};
constexpr const char* kQuestLightNames[] = {"Quest Light R", "Quest Light G", "Quest Light B", "Quest Light Radius"};
constexpr float LightOptions::*kLightChannels[] = {&LightOptions::red, &LightOptions::green, &LightOptions::blue};

// The camera shortcuts: format (with the shortcut index), field of the shortcut.
constexpr struct {
    const char* format;
    float CameraShortcutOptions::*field;
} kShortcutValues[] = {
    {"CameraShortcut %d AxisX", &CameraShortcutOptions::axisX},
    {"CameraShortcut %d AxisY", &CameraShortcutOptions::axisY},
    {"CameraShortcut %d AxisZ", &CameraShortcutOptions::axisZ},
    {"CameraShortcut %d Angle", &CameraShortcutOptions::angle},
    {"CameraShortcut %d Distance", &CameraShortcutOptions::distance},
    {"CameraShortcut %d Pitch", &CameraShortcutOptions::pitch},
};

class Loader {
public:
    Loader(GameOptions* options, CWinRegistry& key) : options_(options), key_(key) {}

    const char* Key(KeyField field) const { return (options_->*field).c_str(); }
    std::string& String(StringField field) { return field(*options_); }

    void Read(const Value& v) {
        if (v.number) options_->*v.number = key_.GetInt(Key(v.key), v.name, options_->*v.number);
        else ReadFloat(v.key, v.name, options_->*v.real);
    }
    void ReadFloat(KeyField key, const char* name, float& field) {
        field = static_cast<float>(key_.GetFloat(Key(key), name, Bits(field)));
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = key_.GetString(Key(v.key), v.name, String(v.field));
    }
    void ReadCameraMotions() {
        for (const auto& v : kCameraMotionValues) ReadFloat(&GameOptions::keyCamera_, v.name, options_->*v.motion.*v.part);
    }

    // Stored as -1 / 1 floats, kept in the registry as booleans.
    void ReadCameraFlip(float& flip, const char* name) {
        int flipped = Bits(flip) == kMinusOneBits ? 1 : 0;
        int value = key_.GetInt(Key(&GameOptions::keyCamera_), name, flipped);
        flip = static_cast<float>(value ? -1 : 1);
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                ReadFloat(&GameOptions::keyCamera_, name, options_->cameraShortcuts_[i].*v.field);
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            float& channel = light.*kLightChannels[c];
            int value = key_.GetInt(Key(&GameOptions::keyGame_), names[c], FtolTimes255(channel));
            channel = UnsignedProduct(static_cast<std::uint32_t>(value), kOver255);
        }
        int radius = key_.GetInt(Key(&GameOptions::keyGame_), names[3], Ftol(light.radius));
        light.radius = Unsigned(static_cast<std::uint32_t>(radius));
    }

private:
    GameOptions* options_;
    CWinRegistry& key_;
};

// The writer: the same tables in the same order, each value through the setter matching its getter.
class Saver {
public:
    Saver(GameOptions* options, CWinRegistry& key) : options_(options), key_(key) {}

    const char* Key(KeyField field) const { return (options_->*field).c_str(); }
    const std::string& String(StringField field) const { return field(*options_); }

    void Write(const Value& v) {
        if (v.number) key_.SetInt(Key(v.key), v.name, options_->*v.number);
        else key_.SetFloat(Key(v.key), v.name, Bits(options_->*v.real));
    }
    template <std::size_t N>
    void Write(const Value (&values)[N]) {
        for (const Value& v : values) Write(v);
    }
    template <std::size_t N>
    void Write(const StringValue (&values)[N]) {
        for (const StringValue& v : values) key_.SetString(Key(v.key), v.name, String(v.field));
    }
    void WriteCameraMotions() {
        for (const auto& v : kCameraMotionValues)
            key_.SetFloat(Key(&GameOptions::keyCamera_), v.name, Bits(options_->*v.motion.*v.part));
    }

    // 1 when the stored float is exactly -1 (compared as bits).
    void WriteCameraFlip(float flip, const char* name) {
        key_.SetInt(Key(&GameOptions::keyCamera_), name, Bits(flip) == kMinusOneBits ? 1 : 0);
    }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                key_.SetFloat(Key(&GameOptions::keyCamera_), name, Bits(options_->cameraShortcuts_[i].*v.field));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(const LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key_.SetInt(Key(&GameOptions::keyGame_), names[c], FtolTimes255(light.*kLightChannels[c]));
        key_.SetInt(Key(&GameOptions::keyGame_), names[3], Ftol(light.radius));
    }

private:
    GameOptions* options_;
    CWinRegistry& key_;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    std::string resolution;   // the game's local is constructed first, read later
    CWinRegistry key;
    key.Open(path);
    Loader l(this, key);

    l.Read(kPathValues);
    l.Read(Value{&GameOptions::keyGeneral_, "DrawingTarget", &GameOptions::driverIndex_});
    {
        // VideoResolution goes through the local string the game built first.
        resolution = key.GetString(l.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
        int x = str::Find(resolution, 'x');
        if (x != -1) {
            resolutionX_ = str::ToInt(str::Left(resolution, x));
            resolutionY_ = std::atoi(str::Right(resolution, static_cast<int>(resolution.size()) - x - 1).c_str());
        }
    }
    l.Read(kVideoValues);
    joystick_ = key.GetInt(l.Key(&GameOptions::keyGeneral_), "Joystick", 0);
    l.Read(kAudioToGeneralValues);
    l.ReadCameraFlip(cameraFlipX_, "CameraFlipXAxis");
    l.ReadCameraFlip(cameraFlipY_, "CameraFlipYAxis");
    l.Read(kCameraValues);
    l.ReadCameraMotions();
    l.ReadCameraShortcuts();
    l.Read(kGameValues);
    l.Read(kCdValues);
    l.Read(kGameValues2);
    l.ReadLight(heroLight_, kHeroLightNames);
    l.ReadLight(questLight_, kQuestLightNames);
    int difficulty = key.GetInt(l.Key(&GameOptions::keyGame_), "Difficulty Level", difficultyStored_);
    difficultyStored_ = difficulty;
    difficulty_ = difficulty;
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    masterServerName_ = key.GetString(l.Key(&GameOptions::keyNetwork_), "Master Server Name", masterServerName_);
    key.Close();
}

void GameOptions::SaveToRegistry(const char* path) {
    std::string resolution;
    CWinRegistry key;
    key.Open(path);
    Saver s(this, key);

    s.Write(kPathValues);
    s.Write(Value{&GameOptions::keyGeneral_, "DrawingTarget", &GameOptions::driverIndex_});
    resolution = std::format("{}x{}", resolutionX_, resolutionY_);
    key.SetString(s.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
    s.Write(kVideoValues);
    key.SetInt(s.Key(&GameOptions::keyGeneral_), "Joystick", 0);
    s.Write(kAudioToGeneralValues);
    s.WriteCameraFlip(cameraFlipX_, "CameraFlipXAxis");
    s.WriteCameraFlip(cameraFlipY_, "CameraFlipYAxis");
    s.Write(kCameraValues);
    s.WriteCameraMotions();
    s.WriteCameraShortcuts();
    s.Write(kGameValues);
    s.Write(kCdValues);
    s.Write(kGameValues2);
    s.WriteLight(heroLight_, kHeroLightNames);
    s.WriteLight(questLight_, kQuestLightNames);
    key.SetInt(s.Key(&GameOptions::keyGame_), "Difficulty Level", difficultyStored_);
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes: the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        key.SetString(s.Key(&GameOptions::keyNetwork_), "Master Server Name", masterServerName_);
    key.Close();
}

} // namespace options
