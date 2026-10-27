// The options loader and writer. See options.h.
// Every value is read through the game's registry key helper (006972A0-006988A0, not rewritten),
// in the original's order; a value that is missing keeps the field's current value, which is
// passed as the default.
#include "options.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "win_registry.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace options {
namespace {

// The string value `name` of `subkey`, or `defaultValue` (CWinRegistry takes it by value).
mfc::CString GetString(CWinRegistry& key, const char* subkey, const char* name, const mfc::CString& defaultValue) {
    return key.GetString(subkey, name, defaultValue);
}

// Writes the string `value` as `name` of `subkey`.
void SetString(CWinRegistry& key, const char* subkey, const char* name, const mfc::CString& value) {
    key.SetString(subkey, name, value);
}

// CString members used here.
struct Text : mfc::CString {
    int FindChar(int c) const;            // 00700E91 Find(TCHAR)
};
int Text::FindChar(int c) const { return mfc::CString::Find(static_cast<char>(c)); }
const Text& Ops(const mfc::CString& s) { return static_cast<const Text&>(s); }

constexpr std::uint32_t kCurrentUser = 0x80000001;   // HKEY_CURRENT_USER
constexpr float kTimes255 = 255.0f;
constexpr float kOver255 = 1.0f / 255.0f;     // 0073C36C (0x3B808081)

float FloatAt(const void* p) { return *static_cast<const float*>(p); }

// _ftol(*value * *factor): the product rounded only by the FPU's precision, then truncated by the
// game's _ftol.
int FtolProduct(const void* value, float factor) {
    return static_cast<int>(gamecrt::Ftol(static_cast<double>(FloatAt(value)) * factor));
}
// _ftol(*value).
int Ftol(const void* value) { return static_cast<int>(gamecrt::Ftol(FloatAt(value))); }
// *out = float(FILD of `value` as an unsigned 64-bit integer, times *factor): the product rounded
// by the FPU's precision, then once to float by FSTP.
void StoreUnsignedProduct(void* out, std::uint32_t value, float factor) {
    *static_cast<float*>(out) = static_cast<float>(x87::FromUnsigned(value) * factor);
}
// *out = float(FILD of `value` as an unsigned 64-bit integer).
void StoreUnsigned(void* out, std::uint32_t value) { *static_cast<float*>(out) = static_cast<float>(x87::FromUnsigned(value)); }

enum Kind { kInt, kDword, kFloat };

// One value: the getter, the subkey field, the value name's address, the field it goes to (and
// whose value is the default).
using KeyField = mfc::CString GameOptions::*;
struct Value {
    Kind kind;
    KeyField key;
    const char* name;
    std::uint16_t field;
};

constexpr Value kVideoValues[] = {
    {kDword, &GameOptions::keyGeneral_, "VideoColorDepthInBits", 0x038},
    {kDword, &GameOptions::keyGeneral_, "VideoZBufferDepthInBits", 0x03C},
    {kDword, &GameOptions::keyGeneral_, "VideoRefreshRate", 0x040},
    {kInt, &GameOptions::keyGeneral_, "FullScreen", 0x044},
};
// Joystick (+0x48, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {kInt, &GameOptions::keyGeneral_, "Video", 0x04C},
    {kInt, &GameOptions::keyGeneral_, "Audio", 0x050},
    {kInt, &GameOptions::keyGeneral_, "AudioStreaming", 0x054},
    {kInt, &GameOptions::keyGeneral_, "AudioSFX", 0x058},
    {kInt, &GameOptions::keyGeneral_, "AudioSpeech", 0x05C},
    {kInt, &GameOptions::keyGeneral_, "Antialiasing", 0x068},
    {kInt, &GameOptions::keyGeneral_, "Dithering", 0x060},
    {kInt, &GameOptions::keyGeneral_, "Specular", 0x064},
    {kInt, &GameOptions::keyGeneral_, "TextureQuality", 0x06C},
    {kInt, &GameOptions::keyGeneral_, "Mipmapping", 0x070},
    {kInt, &GameOptions::keyGeneral_, "Filtering", 0x074},
    {kInt, &GameOptions::keyGeneral_, "PerspectiveCorrection", 0x078},
    {kInt, &GameOptions::keyGeneral_, "ShowParticles", 0x07C},
    {kInt, &GameOptions::keyGeneral_, "FPSIndependentCursor", 0x080},
    {kInt, &GameOptions::keyGeneral_, "AutomaticQuantization", 0x084},
    {kInt, &GameOptions::keyGeneral_, "MovieCapturingFPS", 0x088},
    {kFloat, &GameOptions::keyGeneral_, "Brightness", 0x08C},
    {kFloat, &GameOptions::keyGeneral_, "Contrast", 0x090},
    {kFloat, &GameOptions::keyGeneral_, "Gamma", 0x094},
    {kInt, &GameOptions::keyGeneral_, "Reverse Stereo", 0x098},
    {kInt, &GameOptions::keyShadows_, "ShadowQuality", 0x09C},
    {kInt, &GameOptions::keyShadows_, "HeroShadow", 0x0A0},
    {kInt, &GameOptions::keyShadows_, "UnitShadow", 0x0A4},
    {kInt, &GameOptions::keyShadows_, "BuildingShadow", 0x0A8},
    {kInt, &GameOptions::keyShadows_, "FloraShadow", 0x0AC},
    {kInt, &GameOptions::keyShadows_, "OtherShadow", 0x0B0},
    {kInt, &GameOptions::keyShadows_, "LightingFreq", 0x0B4},
    {kInt, &GameOptions::keyShadows_, "FloraSpecialLight", 0x0B8},
    {kFloat, &GameOptions::keyTerrain_, "LOD1", 0x0BC},
    {kFloat, &GameOptions::keyTerrain_, "LOD2", 0x0C0},
    {kFloat, &GameOptions::keyTerrain_, "BorderFogDistance", 0x0C4},
    {kInt, &GameOptions::keyTerrain_, "EnableWaterWaves", 0x0C8},
    {kInt, &GameOptions::keyTerrain_, "EnableDynamicLights", 0x0D0},
    {kInt, &GameOptions::keyTerrain_, "EnableSelfShadowing", 0x0CC},
    {kInt, &GameOptions::keyTerrain_, "LightingFreq", 0x0D4},
    {kInt, &GameOptions::keyTerrain_, "ShadowingFreq", 0x0D8},
    {kInt, &GameOptions::keyTerrain_, "EnableFootprints", 0x0DC},
    {kInt, &GameOptions::keyTerrain_, "EnableBloodprints", 0x0E0},
    {kInt, &GameOptions::keyTerrain_, "EnableFireprints", 0x0E4},
    {kInt, &GameOptions::keyTerrain_, "EnableBloodprintsRelight", 0x0E8},
    {kInt, &GameOptions::keyStatistics_, "CreateOutputInfo", 0x0EC},
    {kInt, &GameOptions::keyStatistics_, "NumberOfFrames", 0x0F0},
};
constexpr Value kCameraValues[] = {
    {kFloat, &GameOptions::keyCamera_, "CameraMinHeight", 0x0FC},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxHeight", 0x100},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultHeight", 0x104},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultXDeploy", 0x108},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultYDeploy", 0x10C},
    {kFloat, &GameOptions::keyCamera_, "CameraMinDistanceToCarrier", 0x110},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxDistanceToCarrier", 0x114},
    {kFloat, &GameOptions::keyCamera_, "CameraMinLimitDistanceToCarrier", 0x118},
    {kFloat, &GameOptions::keyCamera_, "CameraMaxLimitDistanceToCarrier", 0x11C},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultDistanceToCarrier", 0x120},
    {kFloat, &GameOptions::keyCamera_, "NearClipDistance", 0x1E0},
    {kFloat, &GameOptions::keyCamera_, "FarClipDistance", 0x1E4},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultMinPitch", 0x1D8},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultMaxPitch", 0x1DC},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinDistanceToCarrier", 0x124},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxDistanceToCarrier", 0x128},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinLimitDistanceToCarrier", 0x12C},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxLimitDistanceToCarrier", 0x130},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMinPitch", 0x134},
    {kFloat, &GameOptions::keyCamera_, "CameraBriefingMaxPitch", 0x138},
    {kInt, &GameOptions::keyCamera_, "CameraBorderScrollArea", 0x13C},
    {kInt, &GameOptions::keyCamera_, "CameraFrameSelectionSensetiveArea", 0x140},
    {kFloat, &GameOptions::keyCamera_, "CameraTerrainSensetiveArea", 0x144},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultScrollSpeed", 0x148},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultScrollPowerMouse", 0x14C},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultScrollPowerKeyboard", 0x150},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultRotateSpeed", 0x154},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultRotatePowerMouse", 0x158},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultRotatePowerKeyboard", 0x15C},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultPitchSpeed", 0x160},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultPitchPowerMouse", 0x164},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultPitchPowerKeyboard", 0x168},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultZoomSpeed", 0x16C},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultZoomPowerMouse", 0x170},
    {kFloat, &GameOptions::keyCamera_, "CameraDefaultZoomPowerKeyboard", 0x174},
};
constexpr Value kGameValues[] = {
    {kFloat, &GameOptions::keyGame_, "DefaultWindForce", 0x1E8},
    {kInt, &GameOptions::keyGame_, "DefaultTimeScale", 0x1EC},
    {kInt, &GameOptions::keyGame_, "UseSWCursors", 0x1F0},
    {kInt, &GameOptions::keyGame_, "SelectionType", 0x1F4},
    {kFloat, &GameOptions::keyGame_, "CornerSize", 0x1F8},
    {kInt, &GameOptions::keyGame_, "DrawVisibleOnly", 0x1FC},
    {kInt, &GameOptions::keyGame_, "DrawPath", 0x200},
    {kInt, &GameOptions::keyGame_, "DrawFlyingHP", 0x204},
    {kInt, &GameOptions::keyGame_, "ShowTutorial", 0x208},
    {kInt, &GameOptions::keyGame_, "Autosave", 0x20C},
    {kFloat, &GameOptions::keyGame_, "ToolTip Time", 0x210},
    {kInt, &GameOptions::keyGame_, "SwitchFilters", 0x214},
};
constexpr Value kGameValues2[] = {
    {kDword, &GameOptions::keyGame_, "Keyboard Layout", 0x228},
    {kFloat, &GameOptions::keyGame_, "ObjectsLightingCoeff", 0x22C},
};
constexpr Value kFogValues[] = {
    {kFloat, &GameOptions::keyGame_, "FogNightStartDistance", 0x260},
    {kFloat, &GameOptions::keyGame_, "FogDayStartDistance", 0x264},
};

// One string option: subkey field, value name, field.
using StringField = mfc::CString& (*)(GameOptions&);
template <mfc::CString GameOptions::*Member>
mfc::CString& StringOf(GameOptions& options) { return options.*Member; }
template <int Index>
mfc::CString& CdLabel(GameOptions& options) { return options.cdLabels_[Index]; }
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

// The camera shortcuts: format (with the shortcut index), field offset in the 0x18-byte shortcut.
constexpr struct {
    const char* format;
    std::uint8_t offset;
} kShortcutValues[] = {
    {"CameraShortcut %d AxisX", 0x04},
    {"CameraShortcut %d AxisY", 0x08},
    {"CameraShortcut %d AxisZ", 0x0C},
    {"CameraShortcut %d Angle", 0x10},
    {"CameraShortcut %d Distance", 0x00},
    {"CameraShortcut %d Pitch", 0x14},
};

class Loader {
public:
    Loader(GameOptions* options, CWinRegistry& key) : options_(options), o_(reinterpret_cast<char*>(options)), key_(key) {}

    const char* Key(KeyField field) const { return options_->*field; }
    std::uint32_t Bits(std::size_t offset) const {
        std::uint32_t v;
        std::memcpy(&v, o_ + offset, 4);
        return v;
    }
    void SetBits(std::size_t offset, std::uint32_t v) { std::memcpy(o_ + offset, &v, 4); }
    void SetFloat(std::size_t offset, float v) { std::memcpy(o_ + offset, &v, 4); }
    mfc::CString& String(StringField field) { return field(*options_); }

    void Read(const Value& v) {
        switch (v.kind) {
        case kInt: SetBits(v.field, key_.GetInt(Key(v.key), v.name, static_cast<int>(Bits(v.field)))); break;
        case kDword: SetBits(v.field, key_.GetDWord(Key(v.key), v.name, static_cast<int>(Bits(v.field)))); break;
        case kFloat: SetFloat(v.field, static_cast<float>(key_.GetFloat(Key(v.key), v.name, Bits(v.field)))); break;
        }
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = GetString(key_, Key(v.key), v.name, String(v.field));
    }

    // Stored as -1 / 1 floats, kept in the registry as booleans.
    void ReadCameraFlip(std::size_t offset, const char* name) {
        int flipped = Bits(offset) == 0xBF800000u ? 1 : 0;   // the current value is -1
        int value = key_.GetInt(Key(&GameOptions::keyCamera_), name, flipped);
        SetFloat(offset, static_cast<float>(value ? -1 : 1));
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, v.format, i);
                SetFloat(base + v.offset, static_cast<float>(key_.GetFloat(Key(&GameOptions::keyCamera_), name, Bits(base + v.offset))));
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(std::size_t offset, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            std::size_t f = offset + c * 4;
            int value = key_.GetDWord(Key(&GameOptions::keyGame_), names[c], FtolProduct(o_ + f, kTimes255));
            StoreUnsignedProduct(o_ + f, static_cast<std::uint32_t>(value), kOver255);
        }
        std::size_t f = offset + 12;
        int radius = key_.GetDWord(Key(&GameOptions::keyGame_), names[3], Ftol(o_ + f));
        StoreUnsigned(o_ + f, static_cast<std::uint32_t>(radius));
    }

private:
    GameOptions* options_;
    char* o_;
    CWinRegistry& key_;
};

// The writer: the same tables in the same order, each value through the setter matching its
// getter (GetInt -> SetInt, GetDword -> SetDword).
class Saver {
public:
    Saver(GameOptions* options, CWinRegistry& key) : options_(options), o_(reinterpret_cast<char*>(options)), key_(key) {}

    const char* Key(KeyField field) const { return options_->*field; }
    std::uint32_t Bits(std::size_t offset) const {
        std::uint32_t v;
        std::memcpy(&v, o_ + offset, 4);
        return v;
    }
    const mfc::CString& String(StringField field) const { return field(*options_); }

    void Write(const Value& v) {
        switch (v.kind) {
        case kInt: key_.SetInt(Key(v.key), v.name, static_cast<int>(Bits(v.field))); break;
        case kDword: key_.SetDWord(Key(v.key), v.name, static_cast<int>(Bits(v.field))); break;
        case kFloat: key_.SetFloat(Key(v.key), v.name, Bits(v.field)); break;
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

    // 1 when the stored float is exactly -1 (compared as bits).
    void WriteCameraFlip(std::size_t offset, const char* name) {
        key_.SetInt(Key(&GameOptions::keyCamera_), name, Bits(offset) == 0xBF800000u ? 1 : 0);
    }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, v.format, i);
                key_.SetFloat(Key(&GameOptions::keyCamera_), name, Bits(base + v.offset));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(std::size_t offset, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key_.SetDWord(Key(&GameOptions::keyGame_), names[c], FtolProduct(o_ + offset + c * 4, kTimes255));
        key_.SetDWord(Key(&GameOptions::keyGame_), names[3], Ftol(o_ + offset + 12));
    }

private:
    GameOptions* options_;
    char* o_;
    CWinRegistry& key_;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    mfc::CString resolution;   // the original's local is constructed first, read later
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Loader l(this, key);

    l.Read(kPathValues);
    l.Read(Value{kInt, &GameOptions::keyGeneral_, "DrawingTarget", 0x02C});
    {
        // VideoResolution goes through the local string the original built first.
        resolution = GetString(key, l.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
        int x = Ops(resolution).FindChar('x');
        if (x != -1) {
            l.SetBits(field::kResolutionX, static_cast<std::uint32_t>(std::atoi(resolution.Left(x))));
            l.SetBits(field::kResolutionY,
                      static_cast<std::uint32_t>(std::atoi(Ops(resolution).Right(resolution.GetLength() - x - 1))));
        }
    }
    l.Read(kVideoValues);
    l.SetBits(0x048, static_cast<std::uint32_t>(key.GetInt(l.Key(&GameOptions::keyGeneral_), "Joystick", 0)));
    l.Read(kAudioToGeneralValues);
    l.ReadCameraFlip(field::kCameraFlipX, "CameraFlipXAxis");
    l.ReadCameraFlip(field::kCameraFlipY, "CameraFlipYAxis");
    l.Read(kCameraValues);
    l.ReadCameraShortcuts();
    l.Read(kGameValues);
    l.Read(kCdValues);
    l.Read(kGameValues2);
    l.ReadLight(field::kHeroLight, kHeroLightNames);
    l.ReadLight(field::kQuestLight, kQuestLightNames);
    int difficulty = key.GetDWord(l.Key(&GameOptions::keyGame_), "Difficulty Level", static_cast<int>(l.Bits(field::kDifficulty + 4)));
    l.SetBits(field::kDifficulty + 4, static_cast<std::uint32_t>(difficulty));
    l.SetBits(field::kDifficulty, static_cast<std::uint32_t>(difficulty));
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    l.String(&StringOf<&GameOptions::masterServerName_>) =
        GetString(key, l.Key(&GameOptions::keyNetwork_), "Master Server Name", l.String(&StringOf<&GameOptions::masterServerName_>));
    key.Close();
    key.Destroy();
}
EI_REWRITTEN(0x0045B210, GameOptions::LoadFromRegistry);

void GameOptions::SaveToRegistry(const char* path) {
    mfc::CString resolution;
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Saver s(this, key);

    s.Write(kPathValues);
    s.Write(Value{kInt, &GameOptions::keyGeneral_, "DrawingTarget", 0x02C});
    resolution.Format("%dx%d", static_cast<int>(s.Bits(field::kResolutionX)),
                      static_cast<int>(s.Bits(field::kResolutionY)));
    SetString(key, s.Key(&GameOptions::keyGeneral_), "VideoResolution", resolution);
    s.Write(kVideoValues);
    key.SetInt(s.Key(&GameOptions::keyGeneral_), "Joystick", 0);
    s.Write(kAudioToGeneralValues);
    s.WriteCameraFlip(field::kCameraFlipX, "CameraFlipXAxis");
    s.WriteCameraFlip(field::kCameraFlipY, "CameraFlipYAxis");
    s.Write(kCameraValues);
    s.WriteCameraShortcuts();
    s.Write(kGameValues);
    s.Write(kCdValues);
    s.Write(kGameValues2);
    s.WriteLight(field::kHeroLight, kHeroLightNames);
    s.WriteLight(field::kQuestLight, kQuestLightNames);
    key.SetDWord(s.Key(&GameOptions::keyGame_), "Difficulty Level", static_cast<int>(s.Bits(field::kDifficulty + 4)));
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes: the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        SetString(key, s.Key(&GameOptions::keyNetwork_), "Master Server Name", s.String(&StringOf<&GameOptions::masterServerName_>));
    key.Close();
    key.Destroy();
}
EI_REWRITTEN(0x0045C560, GameOptions::SaveToRegistry);

} // namespace options
