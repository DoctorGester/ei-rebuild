// The options loader and writer. See options.h.
// Every value is read through the game's registry key helper, in the game's order; a value
// that is missing keeps the field's current value, which is passed as the default.
#include "options.h"

#include "../lib/crt.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "win_registry.h"
#include "lib/str.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace options {
namespace {

// The string value `name` of `subkey`, or `defaultValue` (CWinRegistry takes it by value).
std::string GetString(CWinRegistry& key, const char* subkey, const char* name, const std::string& defaultValue) {
    return std::string(key.GetString(subkey, name, defaultValue));
}

// Writes the string `value` as `name` of `subkey`.
void SetString(CWinRegistry& key, const char* subkey, const char* name, const std::string& value) {
    key.SetString(subkey, name, value);
}

constexpr std::uint32_t kCurrentUser = 0x80000001;   // HKEY_CURRENT_USER
constexpr float kTimes255 = 255.0f;
constexpr float kOver255 = 1.0f / 255.0f;     // (0x3B808081)
constexpr std::uint32_t kMinusOneBits = 0xBF800000u;   // -1.0f

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// _ftol(value * factor): the product rounded only by the FPU's precision, then truncated by the
// game's _ftol.
int FtolProduct(float value, float factor) {
    return static_cast<int>(gamecrt::Ftol(static_cast<double>(value) * factor));
}
// _ftol(value).
int Ftol(float value) { return static_cast<int>(gamecrt::Ftol(value)); }
// float(FILD of `value` as an unsigned 64-bit integer, times factor): the product rounded
// by the FPU's precision, then once to float by FSTP.
float UnsignedProduct(std::uint32_t value, float factor) { return static_cast<float>(x87::FromUnsigned(value) * factor); }
// float(FILD of `value` as an unsigned 64-bit integer).
float Unsigned(std::uint32_t value) { return static_cast<float>(x87::FromUnsigned(value)); }

enum Kind { kInt, kDword, kFloat };

// One value: the getter, the subkey field, the value name, the field it goes to (and whose value is
// the default): `number` for kInt / kDword, `real` for kFloat.
using KeyField = std::string GameOptions::*;
struct Value {
    constexpr Value(Kind k, KeyField subkey, const char* valueName, int GameOptions::*field)
        : kind(k), key(subkey), name(valueName), number(field) {}
    constexpr Value(Kind k, KeyField subkey, const char* valueName, float GameOptions::*field)
        : kind(k), key(subkey), name(valueName), real(field) {}
    Kind kind;
    KeyField key;
    const char* name;
    int GameOptions::*number = nullptr;
    float GameOptions::*real = nullptr;
};

constexpr Value kVideoValues[] = {
    {kDword, &GameOptions::keyGeneral_, "VideoColorDepthInBits", &GameOptions::colourDepth_},
    {kDword, &GameOptions::keyGeneral_, "VideoZBufferDepthInBits", &GameOptions::zBufferDepth_},
    {kDword, &GameOptions::keyGeneral_, "VideoRefreshRate", &GameOptions::refreshRate_},
    {kInt, &GameOptions::keyGeneral_, "FullScreen", &GameOptions::fullScreen_},
};
// Joystick (joystick_, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {kInt, &GameOptions::keyGeneral_, "Video", &GameOptions::video_},
    {kInt, &GameOptions::keyGeneral_, "Audio", &GameOptions::audio_},
    {kInt, &GameOptions::keyGeneral_, "AudioStreaming", &GameOptions::audioStreaming_},
    {kInt, &GameOptions::keyGeneral_, "AudioSFX", &GameOptions::audioSfx_},
    {kInt, &GameOptions::keyGeneral_, "AudioSpeech", &GameOptions::audioSpeech_},
    {kInt, &GameOptions::keyGeneral_, "Antialiasing", &GameOptions::antialiasing_},
    {kInt, &GameOptions::keyGeneral_, "Dithering", &GameOptions::dithering_},
    {kInt, &GameOptions::keyGeneral_, "Specular", &GameOptions::specular_},
    {kInt, &GameOptions::keyGeneral_, "TextureQuality", &GameOptions::textureQuality_},
    {kInt, &GameOptions::keyGeneral_, "Mipmapping", &GameOptions::mipmapping_},
    {kInt, &GameOptions::keyGeneral_, "Filtering", &GameOptions::filtering_},
    {kInt, &GameOptions::keyGeneral_, "PerspectiveCorrection", &GameOptions::perspectiveCorrection_},
    {kInt, &GameOptions::keyGeneral_, "ShowParticles", &GameOptions::showParticles_},
    {kInt, &GameOptions::keyGeneral_, "FPSIndependentCursor", &GameOptions::fpsIndependentCursor_},
    {kInt, &GameOptions::keyGeneral_, "AutomaticQuantization", &GameOptions::automaticQuantization_},
    {kInt, &GameOptions::keyGeneral_, "MovieCapturingFPS", &GameOptions::movieCapturingFps_},
    {kFloat, &GameOptions::keyGeneral_, "Brightness", &GameOptions::brightness_},
    {kFloat, &GameOptions::keyGeneral_, "Contrast", &GameOptions::contrast_},
    {kFloat, &GameOptions::keyGeneral_, "Gamma", &GameOptions::gamma_},
    {kInt, &GameOptions::keyGeneral_, "Reverse Stereo", &GameOptions::reverseStereo_},
    {kInt, &GameOptions::keyShadows_, "ShadowQuality", &GameOptions::shadowQuality_},
    {kInt, &GameOptions::keyShadows_, "HeroShadow", &GameOptions::heroShadow_},
    {kInt, &GameOptions::keyShadows_, "UnitShadow", &GameOptions::unitShadow_},
    {kInt, &GameOptions::keyShadows_, "BuildingShadow", &GameOptions::buildingShadow_},
    {kInt, &GameOptions::keyShadows_, "FloraShadow", &GameOptions::floraShadow_},
    {kInt, &GameOptions::keyShadows_, "OtherShadow", &GameOptions::otherShadow_},
    {kInt, &GameOptions::keyShadows_, "LightingFreq", &GameOptions::shadowLightingFreq_},
    {kInt, &GameOptions::keyShadows_, "FloraSpecialLight", &GameOptions::floraSpecialLight_},
    {kFloat, &GameOptions::keyTerrain_, "LOD1", &GameOptions::lod1_},
    {kFloat, &GameOptions::keyTerrain_, "LOD2", &GameOptions::lod2_},
    {kFloat, &GameOptions::keyTerrain_, "BorderFogDistance", &GameOptions::borderFogDistance_},
    {kInt, &GameOptions::keyTerrain_, "EnableWaterWaves", &GameOptions::enableWaterWaves_},
    {kInt, &GameOptions::keyTerrain_, "EnableDynamicLights", &GameOptions::enableDynamicLights_},
    {kInt, &GameOptions::keyTerrain_, "EnableSelfShadowing", &GameOptions::enableSelfShadowing_},
    {kInt, &GameOptions::keyTerrain_, "LightingFreq", &GameOptions::lightingFreq_},
    {kInt, &GameOptions::keyTerrain_, "ShadowingFreq", &GameOptions::shadowingFreq_},
    {kInt, &GameOptions::keyTerrain_, "EnableFootprints", &GameOptions::enableFootprints_},
    {kInt, &GameOptions::keyTerrain_, "EnableBloodprints", &GameOptions::enableBloodprints_},
    {kInt, &GameOptions::keyTerrain_, "EnableFireprints", &GameOptions::enableFireprints_},
    {kInt, &GameOptions::keyTerrain_, "EnableBloodprintsRelight", &GameOptions::enableBloodprintsRelight_},
    {kInt, &GameOptions::keyStatistics_, "CreateOutputInfo", &GameOptions::createOutputInfo_},
    {kInt, &GameOptions::keyStatistics_, "NumberOfFrames", &GameOptions::numberOfFrames_},
};
constexpr Value kCameraValues[] = {
    {kFloat, &GameOptions::keyCamera_, "CameraMinHeight", &GameOptions::cameraMinHeight_},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxHeight", &GameOptions::cameraMaxHeight_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultHeight", &GameOptions::cameraDefaultHeight_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultXDeploy", &GameOptions::cameraDefaultXDeploy_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultYDeploy", &GameOptions::cameraDefaultYDeploy_},
    {kFloat, &GameOptions::keyCamera_, "CameraMinDistanceToCarrier", &GameOptions::cameraMinDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxDistanceToCarrier", &GameOptions::cameraMaxDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraMinLimitDistanceToCarrier", &GameOptions::cameraMinLimitDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxLimitDistanceToCarrier", &GameOptions::cameraMaxLimitDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultDistanceToCarrier", &GameOptions::cameraDefaultDistance_},
    {kFloat, &GameOptions::keyCamera_, "NearClipDistance", &GameOptions::nearClipDistance_},
    {kFloat, &GameOptions::keyCamera_, "FarClipDistance", &GameOptions::farClipDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultMinPitch", &GameOptions::cameraDefaultMinPitch_},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultMaxPitch", &GameOptions::cameraDefaultMaxPitch_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinDistanceToCarrier", &GameOptions::briefingMinDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxDistanceToCarrier", &GameOptions::briefingMaxDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinLimitDistanceToCarrier", &GameOptions::briefingMinLimitDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxLimitDistanceToCarrier", &GameOptions::briefingMaxLimitDistance_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinPitch", &GameOptions::briefingMinPitch_},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxPitch", &GameOptions::briefingMaxPitch_},
    {kInt, &GameOptions::keyCamera_, "CameraBorderScrollArea", &GameOptions::cameraBorderScrollArea_},
    {kInt, &GameOptions::keyCamera_, "CameraFrameSelectionSensetiveArea", &GameOptions::cameraFrameSelectionArea_},
    {kFloat, &GameOptions::keyCamera_, "CameraTerrainSensetiveArea", &GameOptions::cameraTerrainArea_},
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
    {kFloat, &GameOptions::keyGame_, "DefaultWindForce", &GameOptions::defaultWindForce_},
    {kInt, &GameOptions::keyGame_, "DefaultTimeScale", &GameOptions::defaultTimeScale_},
    {kInt, &GameOptions::keyGame_, "UseSWCursors", &GameOptions::useSwCursors_},
    {kInt, &GameOptions::keyGame_, "SelectionType", &GameOptions::selectionType_},
    {kFloat, &GameOptions::keyGame_, "CornerSize", &GameOptions::cornerSize_},
    {kInt, &GameOptions::keyGame_, "DrawVisibleOnly", &GameOptions::drawVisibleOnly_},
    {kInt, &GameOptions::keyGame_, "DrawPath", &GameOptions::drawPath_},
    {kInt, &GameOptions::keyGame_, "DrawFlyingHP", &GameOptions::drawFlyingHp_},
    {kInt, &GameOptions::keyGame_, "ShowTutorial", &GameOptions::showTutorial_},
    {kInt, &GameOptions::keyGame_, "Autosave", &GameOptions::autosave_},
    {kFloat, &GameOptions::keyGame_, "ToolTip Time", &GameOptions::toolTipTime_},
    {kInt, &GameOptions::keyGame_, "SwitchFilters", &GameOptions::switchFilters_},
};
constexpr Value kGameValues2[] = {
    {kDword, &GameOptions::keyGame_, "Keyboard Layout", &GameOptions::keyboardLayout_},
    {kFloat, &GameOptions::keyGame_, "ObjectsLightingCoeff", &GameOptions::objectsLightingCoeff_},
};
constexpr Value kFogValues[] = {
    {kFloat, &GameOptions::keyGame_, "FogNightStartDistance", &GameOptions::fogNightStartDistance_},
    {kFloat, &GameOptions::keyGame_, "FogDayStartDistance", &GameOptions::fogDayStartDistance_},
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
        switch (v.kind) {
        case kInt: options_->*v.number = key_.GetInt(Key(v.key), v.name, options_->*v.number); break;
        case kDword:
            options_->*v.number = static_cast<int>(
                key_.GetDWord(Key(v.key), v.name, static_cast<std::uint32_t>(options_->*v.number)));
            break;
        case kFloat: ReadFloat(v.key, v.name, options_->*v.real); break;
        }
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
        for (const StringValue& v : values) String(v.field) = GetString(key_, Key(v.key), v.name, String(v.field));
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
                gamecrt::sprintf(name, v.format, i);
                ReadFloat(&GameOptions::keyCamera_, name, options_->cameraShortcuts_[i].*v.field);
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            float& channel = light.*kLightChannels[c];
            int value = key_.GetDWord(Key(&GameOptions::keyGame_), names[c], FtolProduct(channel, kTimes255));
            channel = UnsignedProduct(static_cast<std::uint32_t>(value), kOver255);
        }
        int radius = key_.GetDWord(Key(&GameOptions::keyGame_), names[3], Ftol(light.radius));
        light.radius = Unsigned(static_cast<std::uint32_t>(radius));
    }

private:
    GameOptions* options_;
    CWinRegistry& key_;
};

// The writer: the same tables in the same order, each value through the setter matching its
// getter (GetInt -> SetInt, GetDword -> SetDword).
class Saver {
public:
    Saver(GameOptions* options, CWinRegistry& key) : options_(options), key_(key) {}

    const char* Key(KeyField field) const { return (options_->*field).c_str(); }
    const std::string& String(StringField field) const { return field(*options_); }

    void Write(const Value& v) {
        switch (v.kind) {
        case kInt: key_.SetInt(Key(v.key), v.name, options_->*v.number); break;
        case kDword: key_.SetDWord(Key(v.key), v.name, static_cast<std::uint32_t>(options_->*v.number)); break;
        case kFloat: key_.SetFloat(Key(v.key), v.name, Bits(options_->*v.real)); break;
        }
    }
    template <std::size_t N>
    void Write(const Value (&values)[N]) {
        for (const Value& v : values) Write(v);
    }
    template <std::size_t N>
    void Write(const StringValue (&values)[N]) {
        for (const StringValue& v : values) SetString(key_, Key(v.key), v.name, String(v.field));
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
                gamecrt::sprintf(name, v.format, i);
                key_.SetFloat(Key(&GameOptions::keyCamera_), name, Bits(options_->cameraShortcuts_[i].*v.field));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(const LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key_.SetDWord(Key(&GameOptions::keyGame_), names[c], FtolProduct(light.*kLightChannels[c], kTimes255));
        key_.SetDWord(Key(&GameOptions::keyGame_), names[3], Ftol(light.radius));
    }

private:
    GameOptions* options_;
    CWinRegistry& key_;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    std::string resolution;   // the game's local is constructed first, read later
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Loader l(this, key);

    l.Read(kPathValues);
    l.Read(Value{kInt, &GameOptions::keyGeneral_, "DrawingTarget", &GameOptions::driverIndex_});
    {
        // VideoResolution goes through the local string the game built first.
        resolution = GetString(key, l.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
        int x = str::Find(resolution, 'x');
        if (x != -1) {
            resolutionX_ = std::atoi(str::Left(resolution, x).c_str());
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
    int difficulty = static_cast<int>(
        key.GetDWord(l.Key(&GameOptions::keyGame_), "Difficulty Level", static_cast<std::uint32_t>(difficultyStored_)));
    difficultyStored_ = difficulty;
    difficulty_ = difficulty;
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    masterServerName_ = GetString(key, l.Key(&GameOptions::keyNetwork_), "Master Server Name", masterServerName_);
    key.Close();
    key.Destroy();
}

void GameOptions::SaveToRegistry(const char* path) {
    std::string resolution;
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Saver s(this, key);

    s.Write(kPathValues);
    s.Write(Value{kInt, &GameOptions::keyGeneral_, "DrawingTarget", &GameOptions::driverIndex_});
    resolution = str::Format("%dx%d", resolutionX_, resolutionY_);
    SetString(key, s.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
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
    key.SetDWord(s.Key(&GameOptions::keyGame_), "Difficulty Level", static_cast<std::uint32_t>(difficultyStored_));
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes: the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        SetString(key, s.Key(&GameOptions::keyNetwork_), "Master Server Name", masterServerName_);
    key.Close();
    key.Destroy();
}

} // namespace options
