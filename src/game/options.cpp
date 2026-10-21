// The options loader (0045B210) and writer (0045C560). See options.h.
//
// Every value is read through the game's registry key helper (006972A0-006988A0, not rewritten),
// in the original's order; a value that is missing keeps the field's current value, which is
// passed as the default.
#include "options.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "win_registry.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

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
int Text::FindChar(int c) const { return reinterpret_cast<const mfc::CString*>(this)->Find(static_cast<char>(c)); }
const Text& Ops(const mfc::CString& s) { return static_cast<const Text&>(s); }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

constexpr std::uint32_t kCurrentUser = 0x80000001;   // HKEY_CURRENT_USER
constexpr float kTimes255 = 255.0f;           // 0073C370
constexpr float kOver255 = 1.0f / 255.0f;     // 0073C36C (0x3B808081)

float FloatAt(const void* p) { return *static_cast<const float*>(p); }

// _ftol(*value * *factor): the product rounded only by the FPU's precision, then truncated by the
// game's _ftol (006E853C).
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
struct Value {
    Kind kind;
    std::uint8_t key;
    const char* name;
    std::uint16_t field;
};

constexpr Value kVideoValues[] = {
    {kDword, 0x0C, "VideoColorDepthInBits", 0x038},
    {kDword, 0x0C, "VideoZBufferDepthInBits", 0x03C},
    {kDword, 0x0C, "VideoRefreshRate", 0x040},
    {kInt, 0x0C, "FullScreen", 0x044},
};
// Joystick (+0x48, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {kInt, 0x0C, "Video", 0x04C},
    {kInt, 0x0C, "Audio", 0x050},
    {kInt, 0x0C, "AudioStreaming", 0x054},
    {kInt, 0x0C, "AudioSFX", 0x058},
    {kInt, 0x0C, "AudioSpeech", 0x05C},
    {kInt, 0x0C, "Antialiasing", 0x068},
    {kInt, 0x0C, "Dithering", 0x060},
    {kInt, 0x0C, "Specular", 0x064},
    {kInt, 0x0C, "TextureQuality", 0x06C},
    {kInt, 0x0C, "Mipmapping", 0x070},
    {kInt, 0x0C, "Filtering", 0x074},
    {kInt, 0x0C, "PerspectiveCorrection", 0x078},
    {kInt, 0x0C, "ShowParticles", 0x07C},
    {kInt, 0x0C, "FPSIndependentCursor", 0x080},
    {kInt, 0x0C, "AutomaticQuantization", 0x084},
    {kInt, 0x0C, "MovieCapturingFPS", 0x088},
    {kFloat, 0x0C, "Brightness", 0x08C},
    {kFloat, 0x0C, "Contrast", 0x090},
    {kFloat, 0x0C, "Gamma", 0x094},
    {kInt, 0x0C, "Reverse Stereo", 0x098},
    {kInt, 0x14, "ShadowQuality", 0x09C},
    {kInt, 0x14, "HeroShadow", 0x0A0},
    {kInt, 0x14, "UnitShadow", 0x0A4},
    {kInt, 0x14, "BuildingShadow", 0x0A8},
    {kInt, 0x14, "FloraShadow", 0x0AC},
    {kInt, 0x14, "OtherShadow", 0x0B0},
    {kInt, 0x14, "LightingFreq", 0x0B4},
    {kInt, 0x14, "FloraSpecialLight", 0x0B8},
    {kFloat, 0x08, "LOD1", 0x0BC},
    {kFloat, 0x08, "LOD2", 0x0C0},
    {kFloat, 0x08, "BorderFogDistance", 0x0C4},
    {kInt, 0x08, "EnableWaterWaves", 0x0C8},
    {kInt, 0x08, "EnableDynamicLights", 0x0D0},
    {kInt, 0x08, "EnableSelfShadowing", 0x0CC},
    {kInt, 0x08, "LightingFreq", 0x0D4},
    {kInt, 0x08, "ShadowingFreq", 0x0D8},
    {kInt, 0x08, "EnableFootprints", 0x0DC},
    {kInt, 0x08, "EnableBloodprints", 0x0E0},
    {kInt, 0x08, "EnableFireprints", 0x0E4},
    {kInt, 0x08, "EnableBloodprintsRelight", 0x0E8},
    {kInt, 0x00, "CreateOutputInfo", 0x0EC},
    {kInt, 0x00, "NumberOfFrames", 0x0F0},
};
constexpr Value kCameraValues[] = {
    {kFloat, 0x10, "CameraMinHeight", 0x0FC},
    {kFloat, 0x10, "CameraMaxHeight", 0x100},
    {kFloat, 0x10, "CameraDefaultHeight", 0x104},
    {kFloat, 0x10, "CameraDefaultXDeploy", 0x108},
    {kFloat, 0x10, "CameraDefaultYDeploy", 0x10C},
    {kFloat, 0x10, "CameraMinDistanceToCarrier", 0x110},
    {kFloat, 0x10, "CameraMaxDistanceToCarrier", 0x114},
    {kFloat, 0x10, "CameraMinLimitDistanceToCarrier", 0x118},
    {kFloat, 0x10, "CameraMaxLimitDistanceToCarrier", 0x11C},
    {kFloat, 0x10, "CameraDefaultDistanceToCarrier", 0x120},
    {kFloat, 0x10, "NearClipDistance", 0x1E0},
    {kFloat, 0x10, "FarClipDistance", 0x1E4},
    {kFloat, 0x10, "CameraDefaultMinPitch", 0x1D8},
    {kFloat, 0x10, "CameraDefaultMaxPitch", 0x1DC},
    {kFloat, 0x10, "CameraBriefingMinDistanceToCarrier", 0x124},
    {kFloat, 0x10, "CameraBriefingMaxDistanceToCarrier", 0x128},
    {kFloat, 0x10, "CameraBriefingMinLimitDistanceToCarrier", 0x12C},
    {kFloat, 0x10, "CameraBriefingMaxLimitDistanceToCarrier", 0x130},
    {kFloat, 0x10, "CameraBriefingMinPitch", 0x134},
    {kFloat, 0x10, "CameraBriefingMaxPitch", 0x138},
    {kInt, 0x10, "CameraBorderScrollArea", 0x13C},
    {kInt, 0x10, "CameraFrameSelectionSensetiveArea", 0x140},
    {kFloat, 0x10, "CameraTerrainSensetiveArea", 0x144},
    {kFloat, 0x10, "CameraDefaultScrollSpeed", 0x148},
    {kFloat, 0x10, "CameraDefaultScrollPowerMouse", 0x14C},
    {kFloat, 0x10, "CameraDefaultScrollPowerKeyboard", 0x150},
    {kFloat, 0x10, "CameraDefaultRotateSpeed", 0x154},
    {kFloat, 0x10, "CameraDefaultRotatePowerMouse", 0x158},
    {kFloat, 0x10, "CameraDefaultRotatePowerKeyboard", 0x15C},
    {kFloat, 0x10, "CameraDefaultPitchSpeed", 0x160},
    {kFloat, 0x10, "CameraDefaultPitchPowerMouse", 0x164},
    {kFloat, 0x10, "CameraDefaultPitchPowerKeyboard", 0x168},
    {kFloat, 0x10, "CameraDefaultZoomSpeed", 0x16C},
    {kFloat, 0x10, "CameraDefaultZoomPowerMouse", 0x170},
    {kFloat, 0x10, "CameraDefaultZoomPowerKeyboard", 0x174},
};
constexpr Value kGameValues[] = {
    {kFloat, 0x04, "DefaultWindForce", 0x1E8},
    {kInt, 0x04, "DefaultTimeScale", 0x1EC},
    {kInt, 0x04, "UseSWCursors", 0x1F0},
    {kInt, 0x04, "SelectionType", 0x1F4},
    {kFloat, 0x04, "CornerSize", 0x1F8},
    {kInt, 0x04, "DrawVisibleOnly", 0x1FC},
    {kInt, 0x04, "DrawPath", 0x200},
    {kInt, 0x04, "DrawFlyingHP", 0x204},
    {kInt, 0x04, "ShowTutorial", 0x208},
    {kInt, 0x04, "Autosave", 0x20C},
    {kFloat, 0x04, "ToolTip Time", 0x210},
    {kInt, 0x04, "SwitchFilters", 0x214},
};
constexpr Value kGameValues2[] = {
    {kDword, 0x04, "Keyboard Layout", 0x228},
    {kFloat, 0x04, "ObjectsLightingCoeff", 0x22C},
};
constexpr Value kFogValues[] = {
    {kFloat, 0x04, "FogNightStartDistance", 0x260},
    {kFloat, 0x04, "FogDayStartDistance", 0x264},
};

// One string option: subkey field, value name, field.
struct StringValue {
    std::uint8_t key;
    const char* name;
    std::uint16_t field;
};
constexpr StringValue kPathValues[] = {
    {0x18, "Work path", 0x020},
    {0x18, "Resources path", 0x024},
    {0x18, "CD-ROM Path", 0x028},
};
constexpr StringValue kCdValues[] = {
    {0x04, "CD1 Label", 0x218},
    {0x04, "CD2 Label", 0x21C},
    {0x04, "CD3 Label", 0x220},
    {0x04, "Font CharSet", 0x224},
};
constexpr StringValue kDefaultValues[] = {
    {0x04, "DefaultMapName", 0x258},
    {0x04, "DefaultStreamingSound", 0x25C},
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
    Loader(char* options, CWinRegistry& key) : o_(options), key_(key) {}

    const char* Key(std::size_t offset) const { return *reinterpret_cast<const char* const*>(o_ + offset); }
    std::uint32_t Bits(std::size_t offset) const {
        std::uint32_t v;
        std::memcpy(&v, o_ + offset, 4);
        return v;
    }
    void SetBits(std::size_t offset, std::uint32_t v) { std::memcpy(o_ + offset, &v, 4); }
    void SetFloat(std::size_t offset, float v) { std::memcpy(o_ + offset, &v, 4); }
    mfc::CString& String(std::size_t offset) { return *reinterpret_cast<mfc::CString*>(o_ + offset); }

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
        int value = key_.GetInt(Key(field::kKeyCamera), name, flipped);
        SetFloat(offset, static_cast<float>(value ? -1 : 1));
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, v.format, i);
                SetFloat(base + v.offset, static_cast<float>(key_.GetFloat(Key(field::kKeyCamera), name, Bits(base + v.offset))));
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(std::size_t offset, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            std::size_t f = offset + c * 4;
            int value = key_.GetDWord(Key(field::kKeyGame), names[c], FtolProduct(o_ + f, kTimes255));
            StoreUnsignedProduct(o_ + f, static_cast<std::uint32_t>(value), kOver255);
        }
        std::size_t f = offset + 12;
        int radius = key_.GetDWord(Key(field::kKeyGame), names[3], Ftol(o_ + f));
        StoreUnsigned(o_ + f, static_cast<std::uint32_t>(radius));
    }

private:
    char* o_;
    CWinRegistry& key_;
};

// The writer: the same tables in the same order, each value through the setter matching its
// getter (GetInt -> SetInt, GetDword -> SetDword).
class Saver {
public:
    Saver(char* options, CWinRegistry& key) : o_(options), key_(key) {}

    const char* Key(std::size_t offset) const { return *reinterpret_cast<const char* const*>(o_ + offset); }
    std::uint32_t Bits(std::size_t offset) const {
        std::uint32_t v;
        std::memcpy(&v, o_ + offset, 4);
        return v;
    }
    const mfc::CString& String(std::size_t offset) const { return *reinterpret_cast<const mfc::CString*>(o_ + offset); }

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
        key_.SetInt(Key(field::kKeyCamera), name, Bits(offset) == 0xBF800000u ? 1 : 0);
    }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, v.format, i);
                key_.SetFloat(Key(field::kKeyCamera), name, Bits(base + v.offset));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(std::size_t offset, const char* const (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key_.SetDWord(Key(field::kKeyGame), names[c], FtolProduct(o_ + offset + c * 4, kTimes255));
        key_.SetDWord(Key(field::kKeyGame), names[3], Ftol(o_ + offset + 12));
    }

private:
    char* o_;
    CWinRegistry& key_;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    char* o = reinterpret_cast<char*>(this);
    mfc::CString resolution;   // the original's local is constructed first, read later
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Loader l(o, key);

    l.Read(kPathValues);
    l.Read(Value{kInt, 0x0C, "DrawingTarget", 0x02C});
    {
        // VideoResolution goes through the local string the original built first.
        resolution = GetString(key, l.Key(field::kKeyVideo), "VideoResolution", resolution);
        int x = Ops(resolution).FindChar('x');
        if (x != -1) {
            l.SetBits(field::kResolutionX, static_cast<std::uint32_t>(libcrt::Atoi(resolution.Left(x))));
            l.SetBits(field::kResolutionY,
                      static_cast<std::uint32_t>(libcrt::Atoi(Ops(resolution).Right(Length(resolution) - x - 1))));
        }
    }
    l.Read(kVideoValues);
    l.SetBits(0x048, static_cast<std::uint32_t>(key.GetInt(l.Key(field::kKeyVideo), "Joystick", 0)));
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
    int difficulty = key.GetDWord(l.Key(field::kKeyGame), "Difficulty Level", static_cast<int>(l.Bits(field::kDifficulty + 4)));
    l.SetBits(field::kDifficulty + 4, static_cast<std::uint32_t>(difficulty));
    l.SetBits(field::kDifficulty, static_cast<std::uint32_t>(difficulty));
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    l.String(field::kMasterServerName) =
        GetString(key, l.Key(field::kKeyNetwork), "Master Server Name", l.String(field::kMasterServerName));
    key.Close();
    key.Destroy();
}
EI_REPLACE_METHOD(0x0045B210, GameOptions::LoadFromRegistry);

void GameOptions::SaveToRegistry(const char* path) {
    char* o = reinterpret_cast<char*>(this);
    mfc::CString resolution;
    CWinRegistry key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Saver s(o, key);

    s.Write(kPathValues);
    s.Write(Value{kInt, 0x0C, "DrawingTarget", 0x02C});
    resolution.Format("%dx%d", static_cast<int>(s.Bits(field::kResolutionX)),
                      static_cast<int>(s.Bits(field::kResolutionY)));
    SetString(key, s.Key(field::kKeyVideo), "VideoResolution", resolution);
    s.Write(kVideoValues);
    key.SetInt(s.Key(field::kKeyVideo), "Joystick", 0);
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
    key.SetDWord(s.Key(field::kKeyGame), "Difficulty Level", static_cast<int>(s.Bits(field::kDifficulty + 4)));
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes (0045D330): the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        SetString(key, s.Key(field::kKeyNetwork), "Master Server Name", s.String(field::kMasterServerName));
    key.Close();
    key.Destroy();
}
EI_REPLACE_METHOD(0x0045C560, GameOptions::SaveToRegistry);

} // namespace options
