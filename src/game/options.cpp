// The options loader and writer. See options.h.
// Every value is read from the settings file in the game's order; a value that is missing keeps the field's current
// value, which is passed as the default.
#include "options.h"

#include "settings.h"

#include "lib/rounding.h"
#include "lib/str.h"

#include "ei/rebirth.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <new>
#include <string>

namespace options {
namespace {

constexpr float kTimes255 = 255.0f;
constexpr float kOver255 = 1.0f / 255.0f;     // (0x3B808081)
constexpr std::uint32_t kMinusOneBits = 0xBF800000u;   // -1.0f

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// The loader runs at start-up, before the D3D device sets the main thread's 24-bit precision: its products are the
// original's 53-bit ones, double maths (value * 255 exact, then truncated; `value` times factor rounded to a double,
// then to a float). The saver runs at shut-down, at 24-bit precision: value * 255 is a float product there.
int LoadedTimes255(float value) { return lib::Truncate(static_cast<double>(value) * kTimes255); }
int SavedTimes255(float value) { return lib::Truncate(value * kTimes255); }
float UnsignedProduct(std::uint32_t value, float factor) { return static_cast<float>(static_cast<double>(value) * factor); }

using namespace settings;

// One value: the section, the value name, the field it goes to (and whose value is the default): `number` for an
// integer, written as a boolean with `flag` (read as a number: true / false read as 1 / 0), `real` for a float.
constexpr bool kFlag = true;
struct Value {
    constexpr Value(const char* valueSection, const char* valueName, int GameOptions::*field, bool isFlag = false)
        : section(valueSection), name(valueName), number(field), flag(isFlag) {}
    constexpr Value(const char* valueSection, const char* valueName, float GameOptions::*field)
        : section(valueSection), name(valueName), real(field) {}
    const char* section;
    const char* name;
    int GameOptions::*number = nullptr;
    float GameOptions::*real = nullptr;
    bool flag = false;
};

constexpr Value kVideoValues[] = {
    {kVideo, "VideoColorDepthInBits", &GameOptions::colourDepth},
    {kVideo, "VideoZBufferDepthInBits", &GameOptions::zBufferDepth},
    {kVideo, "VideoRefreshRate", &GameOptions::refreshRate},
    {kVideo, "FullScreen", &GameOptions::fullScreen, kFlag},
};
// Joystick (joystick, default 0) comes between these two lists.
constexpr Value kValues[] = {
    {kVideo, "Video", &GameOptions::video},
    {kSound, "Audio", &GameOptions::audio, kFlag},
    {kSound, "AudioStreaming", &GameOptions::audioStreaming},
    {kSound, "AudioSFX", &GameOptions::audioSfx},
    {kSound, "AudioSpeech", &GameOptions::audioSpeech},
    {kVideo, "Antialiasing", &GameOptions::antialiasing, kFlag},
    {kVideo, "Dithering", &GameOptions::dithering, kFlag},
    {kVideo, "Specular", &GameOptions::specular, kFlag},
    {kVideo, "TextureQuality", &GameOptions::textureQuality},
    {kVideo, "Mipmapping", &GameOptions::mipmapping, kFlag},
    {kVideo, "Filtering", &GameOptions::filtering},
    {kVideo, "PerspectiveCorrection", &GameOptions::perspectiveCorrection, kFlag},
    {kVideo, "ShowParticles", &GameOptions::showParticles, kFlag},
    {kVideo, "FPSIndependentCursor", &GameOptions::fpsIndependentCursor, kFlag},
    {kVideo, "AutomaticQuantization", &GameOptions::automaticQuantization, kFlag},
    {kVideo, "MovieCapturingFPS", &GameOptions::movieCapturingFps},
    {kVideo, "Brightness", &GameOptions::brightness},
    {kVideo, "Contrast", &GameOptions::contrast},
    {kVideo, "Gamma", &GameOptions::gamma},
    {kSound, "Reverse Stereo", &GameOptions::reverseStereo, kFlag},
    {kShadows, "ShadowQuality", &GameOptions::shadowQuality},
    {kShadows, "HeroShadow", &GameOptions::heroShadow, kFlag},
    {kShadows, "UnitShadow", &GameOptions::unitShadow, kFlag},
    {kShadows, "BuildingShadow", &GameOptions::buildingShadow, kFlag},
    {kShadows, "FloraShadow", &GameOptions::floraShadow, kFlag},
    {kShadows, "OtherShadow", &GameOptions::otherShadow, kFlag},
    {kShadows, "LightingFreq", &GameOptions::shadowLightingFreq},
    {kShadows, "FloraSpecialLight", &GameOptions::floraSpecialLight, kFlag},
    {kTerrain, "BorderFogDistance", &GameOptions::borderFogDistance},
    {kTerrain, "EnableWaterWaves", &GameOptions::enableWaterWaves, kFlag},
    {kTerrain, "EnableDynamicLights", &GameOptions::enableDynamicLights, kFlag},
    {kTerrain, "EnableSelfShadowing", &GameOptions::enableSelfShadowing, kFlag},
    {kTerrain, "LightingFreq", &GameOptions::lightingFreq},
    {kTerrain, "ShadowingFreq", &GameOptions::shadowingFreq},
    {kTerrain, "EnableFootprints", &GameOptions::enableFootprints, kFlag},
    {kTerrain, "EnableBloodprints", &GameOptions::enableBloodprints, kFlag},
    {kTerrain, "EnableFireprints", &GameOptions::enableFireprints, kFlag},
    {kTerrain, "EnableBloodprintsRelight", &GameOptions::enableBloodprintsRelight, kFlag},
    {kStatistics, "CreateOutputInfo", &GameOptions::createOutputInfo, kFlag},
    {kStatistics, "NumberOfFrames", &GameOptions::numberOfFrames},
};
constexpr Value kCameraValues[] = {
    {kCamera, "CameraMinHeight", &GameOptions::cameraMinHeight},
    {kCamera, "CameraMaxHeight", &GameOptions::cameraMaxHeight},
    {kCamera, "CameraDefaultHeight", &GameOptions::cameraDefaultHeight},
    {kCamera, "CameraDefaultXDeploy", &GameOptions::cameraDefaultXDeploy},
    {kCamera, "CameraDefaultYDeploy", &GameOptions::cameraDefaultYDeploy},
    {kCamera, "CameraMinDistanceToCarrier", &GameOptions::cameraMinDistance},
    {kCamera, "CameraMaxDistanceToCarrier", &GameOptions::cameraMaxDistance},
    {kCamera, "CameraMinLimitDistanceToCarrier", &GameOptions::cameraMinLimitDistance},
    {kCamera, "CameraMaxLimitDistanceToCarrier", &GameOptions::cameraMaxLimitDistance},
    {kCamera, "CameraDefaultDistanceToCarrier", &GameOptions::cameraDefaultDistance},
    {kCamera, "NearClipDistance", &GameOptions::nearClipDistance},
    {kCamera, "FarClipDistance", &GameOptions::farClipDistance},
    {kCamera, "CameraDefaultMinPitch", &GameOptions::cameraDefaultMinPitch},
    {kCamera, "CameraDefaultMaxPitch", &GameOptions::cameraDefaultMaxPitch},
    {kCamera, "CameraBriefingMinDistanceToCarrier", &GameOptions::briefingMinDistance},
    {kCamera, "CameraBriefingMaxDistanceToCarrier", &GameOptions::briefingMaxDistance},
    {kCamera, "CameraBriefingMinLimitDistanceToCarrier", &GameOptions::briefingMinLimitDistance},
    {kCamera, "CameraBriefingMaxLimitDistanceToCarrier", &GameOptions::briefingMaxLimitDistance},
    {kCamera, "CameraBriefingMinPitch", &GameOptions::briefingMinPitch},
    {kCamera, "CameraBriefingMaxPitch", &GameOptions::briefingMaxPitch},
    {kCamera, "CameraBorderScrollArea", &GameOptions::cameraBorderScrollArea},
    {kCamera, "CameraFrameSelectionSensetiveArea", &GameOptions::cameraFrameSelectionArea},
    {kCamera, "CameraTerrainSensetiveArea", &GameOptions::cameraTerrainArea},
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
    {kGame, "DefaultWindForce", &GameOptions::defaultWindForce},
    {kGame, "DefaultTimeScale", &GameOptions::defaultTimeScale},
    {kGame, "UseSWCursors", &GameOptions::useSwCursors, kFlag},
    {kGame, "SelectionType", &GameOptions::selectionType},
    {kGame, "CornerSize", &GameOptions::cornerSize},
    {kGame, "DrawVisibleOnly", &GameOptions::drawVisibleOnly, kFlag},
    {kGame, "DrawPath", &GameOptions::drawPath, kFlag},
    {kGame, "DrawFlyingHP", &GameOptions::drawFlyingHp, kFlag},
    {kGame, "ShowTutorial", &GameOptions::showTutorial, kFlag},
    {kGame, "Autosave", &GameOptions::autosave, kFlag},
    {kGame, "ToolTip Time", &GameOptions::toolTipTime},
    {kGame, "SwitchFilters", &GameOptions::switchFilters, kFlag},
};
constexpr Value kGameValues2[] = {
    {kGame, "Keyboard Layout", &GameOptions::keyboardLayout},
    {kGame, "ObjectsLightingCoeff", &GameOptions::objectsLightingCoeff},
};
constexpr Value kFogValues[] = {
    {kGame, "FogNightStartDistance", &GameOptions::fogNightStartDistance},
    {kGame, "FogDayStartDistance", &GameOptions::fogDayStartDistance},
};

// One string option: section, value name, field.
using StringField = std::string& (*)(GameOptions&);
template <std::string GameOptions::*Member>
std::string& StringOf(GameOptions& options) { return options.*Member; }
template <int Index>
std::string& CdLabel(GameOptions& options) { return options.cdLabels[Index]; }
struct StringValue {
    const char* section;
    const char* name;
    StringField field;
};
constexpr StringValue kPathValues[] = {
    {kPaths, "Work path", &StringOf<&GameOptions::workPath>},
    {kPaths, "Resources path", &StringOf<&GameOptions::resourcesPath>},
    {kPaths, "CD-ROM Path", &StringOf<&GameOptions::cdRomPath>},
};
constexpr StringValue kCdValues[] = {
    {kGame, "CD1 Label", &CdLabel<0>},
    {kGame, "CD2 Label", &CdLabel<1>},
    {kGame, "CD3 Label", &CdLabel<2>},
    {kGame, "Font CharSet", &StringOf<&GameOptions::fontCharSet>},
};
constexpr StringValue kDefaultValues[] = {
    {kGame, "DefaultMapName", &StringOf<&GameOptions::defaultMapName>},
    {kGame, "DefaultStreamingSound", &StringOf<&GameOptions::defaultStreamingSound>},
};

// The lights: r, g, b stored as 0..1 (the file holds 0..255), then the radius.
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
    explicit Loader(GameOptions* newOptions) : options(newOptions) {}

    std::string& String(StringField field) { return field(*options); }

    void Read(const Value& v) {
        if (v.number) options->*v.number = GetInt(v.section, v.name, options->*v.number);
        else options->*v.real = GetFloat(v.section, v.name, options->*v.real);
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = GetString(v.section, v.name, String(v.field));
    }
    void ReadCameraMotions() {
        for (const auto& v : kCameraMotionValues) {
            float& field = options->*v.motion.*v.part;
            field = GetFloat(kCamera, v.name, field);
        }
    }

    // Stored as -1 / 1 floats, kept in the file as booleans.
    void ReadCameraFlip(float& flip, const char* name) {
        int flipped = Bits(flip) == kMinusOneBits ? 1 : 0;
        int value = GetInt(kCamera, name, flipped);
        flip = static_cast<float>(value ? -1 : 1);
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                float& field = options->cameraShortcuts[i].*v.field;
                field = GetFloat(kCamera, name, field);
            }
        }
    }

    // The colour channels are kept in the file as 0..255 integers.
    void ReadLight(LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            float& channel = light.*kLightChannels[c];
            int value = GetInt(kGame, names[c], LoadedTimes255(channel));
            channel = UnsignedProduct(static_cast<std::uint32_t>(value), kOver255);
        }
        int radius = GetInt(kGame, names[3], lib::Truncate(light.radius));
        light.radius = static_cast<float>(static_cast<std::uint32_t>(radius));
    }

private:
    GameOptions* options;
};

// The writer: the same tables in the same order.
class Saver {
public:
    explicit Saver(GameOptions* newOptions) : options(newOptions) {}

    const std::string& String(StringField field) const { return field(*options); }

    void Write(const Value& v) {
        if (v.real) SetFloat(v.section, v.name, options->*v.real);
        else if (v.flag) SetBool(v.section, v.name, options->*v.number != 0);
        else SetInt(v.section, v.name, options->*v.number);
    }
    template <std::size_t N>
    void Write(const Value (&values)[N]) {
        for (const Value& v : values) Write(v);
    }
    template <std::size_t N>
    void Write(const StringValue (&values)[N]) {
        for (const StringValue& v : values) SetString(v.section, v.name, String(v.field));
    }
    void WriteCameraMotions() {
        for (const auto& v : kCameraMotionValues) SetFloat(kCamera, v.name, options->*v.motion.*v.part);
    }

    // true when the stored float is exactly -1 (compared as bits).
    void WriteCameraFlip(float flip, const char* name) { SetBool(kCamera, name, Bits(flip) == kMinusOneBits); }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                SetFloat(kCamera, name, options->cameraShortcuts[i].*v.field);
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(const LightOptions& light, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) SetInt(kGame, names[c], SavedTimes255(light.*kLightChannels[c]));
        SetInt(kGame, names[3], lib::Truncate(light.radius));
    }

private:
    GameOptions* options;
};

} // namespace

void GameOptions::Load() {
    Loader l(this);
    l.Read(kPathValues);
    l.Read(Value{kVideo, "DrawingTarget", &GameOptions::driverIndex});
    {
        const std::string resolution = GetString(kVideo, "VideoResolution", std::string());
        int x = str::Find(resolution, 'x');
        if (x != -1) {
            resolutionX = str::ToInt(str::Left(resolution, x));
            resolutionY = std::atoi(str::Right(resolution, static_cast<int>(resolution.size()) - x - 1).c_str());
        }
    }
    l.Read(kVideoValues);
    windowWidth = resolutionX;
    windowHeight = resolutionY;
    joystick = GetInt(kGame, "Joystick", 0);
    l.Read(kValues);
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
    int newDifficulty = GetInt(kGame, "Difficulty Level", difficultyStored);
    difficultyStored = newDifficulty;
    difficulty = newDifficulty;
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    masterServerName = GetString(kNetwork, "Master Server Name", masterServerName);
}

void GameOptions::Save() {
    Saver s(this);
    s.Write(kPathValues);
    s.Write(Value{kVideo, "DrawingTarget", &GameOptions::driverIndex});
    SetString(kVideo, "VideoResolution",
              std::format("{}x{}", windowWidth ? windowWidth : resolutionX, windowHeight ? windowHeight : resolutionY));
    s.Write(kVideoValues);
    SetBool(kGame, "Joystick", false);
    s.Write(kValues);
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
    SetInt(kGame, "Difficulty Level", difficultyStored);
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes: the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        SetString(kNetwork, "Master Server Name", masterServerName);
    settings::Save();
}

} // namespace options
