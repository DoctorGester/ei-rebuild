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
    {&GameOptions::keyGeneral, "VideoColorDepthInBits", &GameOptions::colourDepth},
    {&GameOptions::keyGeneral, "VideoZBufferDepthInBits", &GameOptions::zBufferDepth},
    {&GameOptions::keyGeneral, "VideoRefreshRate", &GameOptions::refreshRate},
    {&GameOptions::keyGeneral, "FullScreen", &GameOptions::fullScreen},
};
// Joystick (joystick, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {&GameOptions::keyGeneral, "Video", &GameOptions::video},
    {&GameOptions::keyGeneral, "Audio", &GameOptions::audio},
    {&GameOptions::keyGeneral, "AudioStreaming", &GameOptions::audioStreaming},
    {&GameOptions::keyGeneral, "AudioSFX", &GameOptions::audioSfx},
    {&GameOptions::keyGeneral, "AudioSpeech", &GameOptions::audioSpeech},
    {&GameOptions::keyGeneral, "Antialiasing", &GameOptions::antialiasing},
    {&GameOptions::keyGeneral, "Dithering", &GameOptions::dithering},
    {&GameOptions::keyGeneral, "Specular", &GameOptions::specular},
    {&GameOptions::keyGeneral, "TextureQuality", &GameOptions::textureQuality},
    {&GameOptions::keyGeneral, "Mipmapping", &GameOptions::mipmapping},
    {&GameOptions::keyGeneral, "Filtering", &GameOptions::filtering},
    {&GameOptions::keyGeneral, "PerspectiveCorrection", &GameOptions::perspectiveCorrection},
    {&GameOptions::keyGeneral, "ShowParticles", &GameOptions::showParticles},
    {&GameOptions::keyGeneral, "FPSIndependentCursor", &GameOptions::fpsIndependentCursor},
    {&GameOptions::keyGeneral, "AutomaticQuantization", &GameOptions::automaticQuantization},
    {&GameOptions::keyGeneral, "MovieCapturingFPS", &GameOptions::movieCapturingFps},
    {&GameOptions::keyGeneral, "Brightness", &GameOptions::brightness},
    {&GameOptions::keyGeneral, "Contrast", &GameOptions::contrast},
    {&GameOptions::keyGeneral, "Gamma", &GameOptions::gamma},
    {&GameOptions::keyGeneral, "Reverse Stereo", &GameOptions::reverseStereo},
    {&GameOptions::keyShadows, "ShadowQuality", &GameOptions::shadowQuality},
    {&GameOptions::keyShadows, "HeroShadow", &GameOptions::heroShadow},
    {&GameOptions::keyShadows, "UnitShadow", &GameOptions::unitShadow},
    {&GameOptions::keyShadows, "BuildingShadow", &GameOptions::buildingShadow},
    {&GameOptions::keyShadows, "FloraShadow", &GameOptions::floraShadow},
    {&GameOptions::keyShadows, "OtherShadow", &GameOptions::otherShadow},
    {&GameOptions::keyShadows, "LightingFreq", &GameOptions::shadowLightingFreq},
    {&GameOptions::keyShadows, "FloraSpecialLight", &GameOptions::floraSpecialLight},
    {&GameOptions::keyTerrain, "LOD1", &GameOptions::lod1},
    {&GameOptions::keyTerrain, "LOD2", &GameOptions::lod2},
    {&GameOptions::keyTerrain, "BorderFogDistance", &GameOptions::borderFogDistance},
    {&GameOptions::keyTerrain, "EnableWaterWaves", &GameOptions::enableWaterWaves},
    {&GameOptions::keyTerrain, "EnableDynamicLights", &GameOptions::enableDynamicLights},
    {&GameOptions::keyTerrain, "EnableSelfShadowing", &GameOptions::enableSelfShadowing},
    {&GameOptions::keyTerrain, "LightingFreq", &GameOptions::lightingFreq},
    {&GameOptions::keyTerrain, "ShadowingFreq", &GameOptions::shadowingFreq},
    {&GameOptions::keyTerrain, "EnableFootprints", &GameOptions::enableFootprints},
    {&GameOptions::keyTerrain, "EnableBloodprints", &GameOptions::enableBloodprints},
    {&GameOptions::keyTerrain, "EnableFireprints", &GameOptions::enableFireprints},
    {&GameOptions::keyTerrain, "EnableBloodprintsRelight", &GameOptions::enableBloodprintsRelight},
    {&GameOptions::keyStatistics, "CreateOutputInfo", &GameOptions::createOutputInfo},
    {&GameOptions::keyStatistics, "NumberOfFrames", &GameOptions::numberOfFrames},
};
constexpr Value kCameraValues[] = {
    {&GameOptions::keyCamera, "CameraMinHeight", &GameOptions::cameraMinHeight},
    {&GameOptions::keyCamera, "CameraMaxHeight", &GameOptions::cameraMaxHeight},
    {&GameOptions::keyCamera, "CameraDefaultHeight", &GameOptions::cameraDefaultHeight},
    {&GameOptions::keyCamera, "CameraDefaultXDeploy", &GameOptions::cameraDefaultXDeploy},
    {&GameOptions::keyCamera, "CameraDefaultYDeploy", &GameOptions::cameraDefaultYDeploy},
    {&GameOptions::keyCamera, "CameraMinDistanceToCarrier", &GameOptions::cameraMinDistance},
    {&GameOptions::keyCamera, "CameraMaxDistanceToCarrier", &GameOptions::cameraMaxDistance},
    {&GameOptions::keyCamera, "CameraMinLimitDistanceToCarrier", &GameOptions::cameraMinLimitDistance},
    {&GameOptions::keyCamera, "CameraMaxLimitDistanceToCarrier", &GameOptions::cameraMaxLimitDistance},
    {&GameOptions::keyCamera, "CameraDefaultDistanceToCarrier", &GameOptions::cameraDefaultDistance},
    {&GameOptions::keyCamera, "NearClipDistance", &GameOptions::nearClipDistance},
    {&GameOptions::keyCamera, "FarClipDistance", &GameOptions::farClipDistance},
    {&GameOptions::keyCamera, "CameraDefaultMinPitch", &GameOptions::cameraDefaultMinPitch},
    {&GameOptions::keyCamera, "CameraDefaultMaxPitch", &GameOptions::cameraDefaultMaxPitch},
    {&GameOptions::keyCamera, "CameraBriefingMinDistanceToCarrier", &GameOptions::briefingMinDistance},
    {&GameOptions::keyCamera, "CameraBriefingMaxDistanceToCarrier", &GameOptions::briefingMaxDistance},
    {&GameOptions::keyCamera, "CameraBriefingMinLimitDistanceToCarrier", &GameOptions::briefingMinLimitDistance},
    {&GameOptions::keyCamera, "CameraBriefingMaxLimitDistanceToCarrier", &GameOptions::briefingMaxLimitDistance},
    {&GameOptions::keyCamera, "CameraBriefingMinPitch", &GameOptions::briefingMinPitch},
    {&GameOptions::keyCamera, "CameraBriefingMaxPitch", &GameOptions::briefingMaxPitch},
    {&GameOptions::keyCamera, "CameraBorderScrollArea", &GameOptions::cameraBorderScrollArea},
    {&GameOptions::keyCamera, "CameraFrameSelectionSensetiveArea", &GameOptions::cameraFrameSelectionArea},
    {&GameOptions::keyCamera, "CameraTerrainSensetiveArea", &GameOptions::cameraTerrainArea},
};
// Read right after kCameraValues: "CameraDefault<Scroll|Rotate|Pitch|Zoom><Speed|PowerMouse|PowerKeyboard>".
constexpr struct {
    const char* name;
    CameraMotionOptions GameOptions::*motion;
    float CameraMotionOptions::*part;
} kCameraMotionValues[] = {
    {"CameraDefaultScrollSpeed", &GameOptions::cameraScroll, &CameraMotionOptions::speed},
    {"CameraDefaultScrollPowerMouse", &GameOptions::cameraScroll, &CameraMotionOptions::powerMouse},
    {"CameraDefaultScrollPowerKeyboard", &GameOptions::cameraScroll, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultRotateSpeed", &GameOptions::cameraRotate, &CameraMotionOptions::speed},
    {"CameraDefaultRotatePowerMouse", &GameOptions::cameraRotate, &CameraMotionOptions::powerMouse},
    {"CameraDefaultRotatePowerKeyboard", &GameOptions::cameraRotate, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultPitchSpeed", &GameOptions::cameraPitch, &CameraMotionOptions::speed},
    {"CameraDefaultPitchPowerMouse", &GameOptions::cameraPitch, &CameraMotionOptions::powerMouse},
    {"CameraDefaultPitchPowerKeyboard", &GameOptions::cameraPitch, &CameraMotionOptions::powerKeyboard},
    {"CameraDefaultZoomSpeed", &GameOptions::cameraZoom, &CameraMotionOptions::speed},
    {"CameraDefaultZoomPowerMouse", &GameOptions::cameraZoom, &CameraMotionOptions::powerMouse},
    {"CameraDefaultZoomPowerKeyboard", &GameOptions::cameraZoom, &CameraMotionOptions::powerKeyboard},
};
constexpr Value kGameValues[] = {
    {&GameOptions::keyGame, "DefaultWindForce", &GameOptions::defaultWindForce},
    {&GameOptions::keyGame, "DefaultTimeScale", &GameOptions::defaultTimeScale},
    {&GameOptions::keyGame, "UseSWCursors", &GameOptions::useSwCursors},
    {&GameOptions::keyGame, "SelectionType", &GameOptions::selectionType},
    {&GameOptions::keyGame, "CornerSize", &GameOptions::cornerSize},
    {&GameOptions::keyGame, "DrawVisibleOnly", &GameOptions::drawVisibleOnly},
    {&GameOptions::keyGame, "DrawPath", &GameOptions::drawPath},
    {&GameOptions::keyGame, "DrawFlyingHP", &GameOptions::drawFlyingHp},
    {&GameOptions::keyGame, "ShowTutorial", &GameOptions::showTutorial},
    {&GameOptions::keyGame, "Autosave", &GameOptions::autosave},
    {&GameOptions::keyGame, "ToolTip Time", &GameOptions::toolTipTime},
    {&GameOptions::keyGame, "SwitchFilters", &GameOptions::switchFilters},
};
constexpr Value kGameValues2[] = {
    {&GameOptions::keyGame, "Keyboard Layout", &GameOptions::keyboardLayout},
    {&GameOptions::keyGame, "ObjectsLightingCoeff", &GameOptions::objectsLightingCoeff},
};
constexpr Value kFogValues[] = {
    {&GameOptions::keyGame, "FogNightStartDistance", &GameOptions::fogNightStartDistance},
    {&GameOptions::keyGame, "FogDayStartDistance", &GameOptions::fogDayStartDistance},
};

// One string option: subkey field, value name, field.
using StringField = std::string& (*)(GameOptions&);
template <std::string GameOptions::*Member>
std::string& StringOf(GameOptions& options) { return options.*Member; }
template <int Index>
std::string& CdLabel(GameOptions& options) { return options.cdLabels[Index]; }
struct StringValue {
    KeyField key;
    const char* name;
    StringField field;
};
constexpr StringValue kPathValues[] = {
    {&GameOptions::keyPaths, "Work path", &StringOf<&GameOptions::workPath>},
    {&GameOptions::keyPaths, "Resources path", &StringOf<&GameOptions::resourcesPath>},
    {&GameOptions::keyPaths, "CD-ROM Path", &StringOf<&GameOptions::cdRomPath>},
};
constexpr StringValue kCdValues[] = {
    {&GameOptions::keyGame, "CD1 Label", &CdLabel<0>},
    {&GameOptions::keyGame, "CD2 Label", &CdLabel<1>},
    {&GameOptions::keyGame, "CD3 Label", &CdLabel<2>},
    {&GameOptions::keyGame, "Font CharSet", &StringOf<&GameOptions::fontCharSet>},
};
constexpr StringValue kDefaultValues[] = {
    {&GameOptions::keyGame, "DefaultMapName", &StringOf<&GameOptions::defaultMapName>},
    {&GameOptions::keyGame, "DefaultStreamingSound", &StringOf<&GameOptions::defaultStreamingSound>},
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
    Loader(GameOptions* newOptions, WinRegistry& newKey) : options(newOptions), key(newKey) {}

    const char* Key(KeyField field) const { return (options->*field).c_str(); }
    std::string& String(StringField field) { return field(*options); }

    void Read(const Value& v) {
        if (v.number) options->*v.number = key.GetInt(Key(v.key), v.name, options->*v.number);
        else ReadFloat(v.key, v.name, options->*v.real);
    }
    void ReadFloat(KeyField keyField, const char* name, float& field) {
        field = static_cast<float>(key.GetFloat(Key(keyField), name, Bits(field)));
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = key.GetString(Key(v.key), v.name, String(v.field));
    }
    void ReadCameraMotions() {
        for (const auto& v : kCameraMotionValues) ReadFloat(&GameOptions::keyCamera, v.name, options->*v.motion.*v.part);
    }

    // Stored as -1 / 1 floats, kept in the registry as booleans.
    void ReadCameraFlip(float& flip, const char* name) {
        int flipped = Bits(flip) == kMinusOneBits ? 1 : 0;
        int value = key.GetInt(Key(&GameOptions::keyCamera), name, flipped);
        flip = static_cast<float>(value ? -1 : 1);
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                ReadFloat(&GameOptions::keyCamera, name, options->cameraShortcuts[i].*v.field);
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            float& channel = light.*kLightChannels[c];
            int value = key.GetInt(Key(&GameOptions::keyGame), names[c], FtolTimes255(channel));
            channel = UnsignedProduct(static_cast<std::uint32_t>(value), kOver255);
        }
        int radius = key.GetInt(Key(&GameOptions::keyGame), names[3], Ftol(light.radius));
        light.radius = Unsigned(static_cast<std::uint32_t>(radius));
    }

private:
    GameOptions* options;
    WinRegistry& key;
};

// The writer: the same tables in the same order, each value through the setter matching its getter.
class Saver {
public:
    Saver(GameOptions* newOptions, WinRegistry& newKey) : options(newOptions), key(newKey) {}

    const char* Key(KeyField field) const { return (options->*field).c_str(); }
    const std::string& String(StringField field) const { return field(*options); }

    void Write(const Value& v) {
        if (v.number) key.SetInt(Key(v.key), v.name, options->*v.number);
        else key.SetFloat(Key(v.key), v.name, Bits(options->*v.real));
    }
    template <std::size_t N>
    void Write(const Value (&values)[N]) {
        for (const Value& v : values) Write(v);
    }
    template <std::size_t N>
    void Write(const StringValue (&values)[N]) {
        for (const StringValue& v : values) key.SetString(Key(v.key), v.name, String(v.field));
    }
    void WriteCameraMotions() {
        for (const auto& v : kCameraMotionValues)
            key.SetFloat(Key(&GameOptions::keyCamera), v.name, Bits(options->*v.motion.*v.part));
    }

    // 1 when the stored float is exactly -1 (compared as bits).
    void WriteCameraFlip(float flip, const char* name) {
        key.SetInt(Key(&GameOptions::keyCamera), name, Bits(flip) == kMinusOneBits ? 1 : 0);
    }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                key.SetFloat(Key(&GameOptions::keyCamera), name, Bits(options->cameraShortcuts[i].*v.field));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(const LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key.SetInt(Key(&GameOptions::keyGame), names[c], FtolTimes255(light.*kLightChannels[c]));
        key.SetInt(Key(&GameOptions::keyGame), names[3], Ftol(light.radius));
    }

private:
    GameOptions* options;
    WinRegistry& key;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    std::string resolution;   // the game's local is constructed first, read later
    WinRegistry key;
    key.Open(path);
    Loader l(this, key);

    l.Read(kPathValues);
    l.Read(Value{&GameOptions::keyGeneral, "DrawingTarget", &GameOptions::driverIndex});
    {
        // VideoResolution goes through the local string the game built first.
        resolution = key.GetString(l.Key(&GameOptions::keyGeneral), "VideoResolution", resolution);
        int x = str::Find(resolution, 'x');
        if (x != -1) {
            resolutionX = str::ToInt(str::Left(resolution, x));
            resolutionY = std::atoi(str::Right(resolution, static_cast<int>(resolution.size()) - x - 1).c_str());
        }
    }
    l.Read(kVideoValues);
    joystick = key.GetInt(l.Key(&GameOptions::keyGeneral), "Joystick", 0);
    l.Read(kAudioToGeneralValues);
    l.ReadCameraFlip(cameraFlipX, "CameraFlipXAxis");
    l.ReadCameraFlip(cameraFlipY, "CameraFlipYAxis");
    l.Read(kCameraValues);
    l.ReadCameraMotions();
    l.ReadCameraShortcuts();
    l.Read(kGameValues);
    l.Read(kCdValues);
    l.Read(kGameValues2);
    l.ReadLight(heroLight, kHeroLightNames);
    l.ReadLight(questLight, kQuestLightNames);
    int newDifficulty = key.GetInt(l.Key(&GameOptions::keyGame), "Difficulty Level", difficultyStored);
    difficultyStored = newDifficulty;
    difficulty = newDifficulty;
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    masterServerName = key.GetString(l.Key(&GameOptions::keyNetwork), "Master Server Name", masterServerName);
    key.Close();
}

void GameOptions::SaveToRegistry(const char* path) {
    std::string resolution;
    WinRegistry key;
    key.Open(path);
    Saver s(this, key);

    s.Write(kPathValues);
    s.Write(Value{&GameOptions::keyGeneral, "DrawingTarget", &GameOptions::driverIndex});
    resolution = std::format("{}x{}", resolutionX, resolutionY);
    key.SetString(s.Key(&GameOptions::keyGeneral), "VideoResolution", resolution);
    s.Write(kVideoValues);
    key.SetInt(s.Key(&GameOptions::keyGeneral), "Joystick", 0);
    s.Write(kAudioToGeneralValues);
    s.WriteCameraFlip(cameraFlipX, "CameraFlipXAxis");
    s.WriteCameraFlip(cameraFlipY, "CameraFlipYAxis");
    s.Write(kCameraValues);
    s.WriteCameraMotions();
    s.WriteCameraShortcuts();
    s.Write(kGameValues);
    s.Write(kCdValues);
    s.Write(kGameValues2);
    s.WriteLight(heroLight, kHeroLightNames);
    s.WriteLight(questLight, kQuestLightNames);
    key.SetInt(s.Key(&GameOptions::keyGame), "Difficulty Level", difficultyStored);
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes: the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        key.SetString(s.Key(&GameOptions::keyNetwork), "Master Server Name", masterServerName);
    key.Close();
}

} // namespace options
