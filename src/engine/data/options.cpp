// The options loader and writer. See options.h.
// Every value is read from the settings file in the game's order; a value that is missing takes its catalog row's
// default (settings_catalog.inc).
#include "engine/data/options.h"

#include "engine/data/settings.h"

#include "lib/str.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <new>
#include <string>

namespace options {
namespace {

using namespace settings;

// One value: the section, the value name, the field it goes to: `number` for an integer, written as a boolean with
// `flag` (read as a number: true / false read as 1 / 0), `real` for a float.
constexpr bool kFlag = true;
struct Value {
    constexpr Value(const char* valueSection, const char* valueName, int GameOptions::*field, bool isFlag = false)
        : section(valueSection), name(valueName), number(field), flag(isFlag) {}
    constexpr Value(const char* valueSection, const char* valueName, float GameOptions::*field)
        : section(valueSection), name(valueName), real(field) {}
    const char* section = nullptr;
    const char* name = nullptr;
    int GameOptions::*number = nullptr;
    float GameOptions::*real = nullptr;
    bool flag = false;
};

constexpr Value kValues[] = {
    {kVideo, "FullScreen", &GameOptions::fullScreen, kFlag},
    {kVideo, "Video", &GameOptions::video},
    {kSound, "Audio", &GameOptions::audio, kFlag},
    {kSound, "AudioStreaming", &GameOptions::audioStreaming},
    {kSound, "AudioSFX", &GameOptions::audioSfx},
    {kSound, "AudioSpeech", &GameOptions::audioSpeech},
    {kVideo, "Brightness", &GameOptions::brightness},
    {kVideo, "Gamma", &GameOptions::gamma},
    {kSound, "Reverse Stereo", &GameOptions::reverseStereo, kFlag},
    {kShadows, "Shadows", &GameOptions::shadows, kFlag},
    {kTerrain, "BorderFogDistance", &GameOptions::borderFogDistance},
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
    const char* name = nullptr;
    CameraMotionOptions GameOptions::*motion = nullptr;
    float CameraMotionOptions::*part = nullptr;
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
    {kGame, "SelectionType", &GameOptions::selectionType},
    {kGame, "DrawPath", &GameOptions::drawPath, kFlag},
    {kGame, "DrawFlyingHP", &GameOptions::drawFlyingHp, kFlag},
    {kGame, "ShowTutorial", &GameOptions::showTutorial, kFlag},
    {kGame, "Autosave", &GameOptions::autosave, kFlag},
    {kGame, "ToolTip Time", &GameOptions::toolTipTime},
    {kGame, "SwitchFilters", &GameOptions::switchFilters, kFlag},
};

// One string option: section, value name, field.
using StringField = std::string& (*)(GameOptions&);
template <std::string GameOptions::*Member>
std::string& StringOf(GameOptions& options) { return options.*Member; }
struct StringValue {
    const char* section = nullptr;
    const char* name = nullptr;
    StringField field = nullptr;
};
constexpr StringValue kPathValues[] = {
    {kPaths, "Work path", &StringOf<&GameOptions::workPath>},
    {kPaths, "Resources path", &StringOf<&GameOptions::resourcesPath>},
};

// The camera shortcuts: format (with the shortcut index), field of the shortcut.
constexpr struct {
    const char* format = nullptr;
    float CameraShortcutOptions::*field = nullptr;
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
        if (v.number) options->*v.number = GetInt(v.section, v.name);
        else options->*v.real = GetFloat(v.section, v.name);
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = GetString(v.section, v.name);
    }
    void ReadCameraMotions() {
        for (const auto& v : kCameraMotionValues) options->*v.motion.*v.part = GetFloat(kCamera, v.name);
    }

    // Stored as -1 / 1 floats, kept in the file as booleans.
    void ReadCameraFlip(float& flip, const char* name) { flip = GetInt(kCamera, name) ? -1.0f : 1.0f; }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (int i = 0; i < kCameraShortcuts; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                options->cameraShortcuts[i].*v.field = GetFloat(kCamera, name);
            }
        }
    }

private:
    GameOptions* options = nullptr;
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
    void WriteCameraFlip(float flip, const char* name) { SetBool(kCamera, name, flip == -1.0f); }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (int i = 0; i < kCameraShortcuts; ++i) {
            for (const auto& v : kShortcutValues) {
                std::snprintf(name, sizeof(name), v.format, i);
                SetFloat(kCamera, name, options->cameraShortcuts[i].*v.field);
            }
        }
    }

private:
    GameOptions* options = nullptr;
};

} // namespace

void GameOptions::Load() {
    Loader l(this);
    l.Read(kPathValues);
    {
        const std::string resolution = GetString(kVideo, "VideoResolution");
        int x = str::Find(resolution, 'x');
        if (x != -1) {
            resolutionX = str::ToInt(str::Left(resolution, x));
            resolutionY = str::ToInt(str::Right(resolution, static_cast<int>(resolution.size()) - x - 1));
        }
    }
    l.Read(kValues);
    l.ReadCameraFlip(cameraFlipX, "CameraFlipXAxis");
    l.ReadCameraFlip(cameraFlipY, "CameraFlipYAxis");
    l.Read(kCameraValues);
    l.ReadCameraMotions();
    l.ReadCameraShortcuts();
    l.Read(kGameValues);
    difficultyStored = GetInt(kGame, "Difficulty Level");
    difficulty = difficultyStored;
    masterServerName = GetString(kNetwork, "Master Server Name");
}

void GameOptions::Save() {
    Saver s(this);
    s.Write(kPathValues);
    SetString(kVideo, "VideoResolution", std::format("{}x{}", resolutionX, resolutionY));
    s.Write(kValues);
    s.WriteCameraFlip(cameraFlipX, "CameraFlipXAxis");
    s.WriteCameraFlip(cameraFlipY, "CameraFlipYAxis");
    s.Write(kCameraValues);
    s.WriteCameraMotions();
    s.WriteCameraShortcuts();
    s.Write(kGameValues);
    SetInt(kGame, "Difficulty Level", difficultyStored);
    // The Master Server Name is not saved (EI_Plugin's NewMaster, always on, NOPs these 42 bytes).
    settings::Save();
}

// --- The global --------------------------------------------------------------------------------

// The game's options: constructed by the C++ runtime before the static initialisers and WinMain (Load fills them),
// destroyed after the game's exit functions (ShutDown's Save has written them back).
GameOptions g_options;

} // namespace options
