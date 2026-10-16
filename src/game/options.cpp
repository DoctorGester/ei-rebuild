// The options loader (0045B210) and writer (0045C560). See options.h.
//
// Every value is read through the game's registry key helper (006972A0-006988A0, not rewritten),
// in the original's order; a value that is missing keeps the field's current value, which is
// passed as the default. Value names are passed by their original addresses.
#include "options.h"
#include "game_strings.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "originals_library.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

namespace options {
namespace {

// A CString passed by value to original code (which destroys it): the bits of a string that holds a
// reference of its own.
struct RawString {
    char* data;
};

// The game's registry key wrapper (an HKEY). Not rewritten.
struct RegistryKey {
    void* key;

    void Construct();                                                         // 006972A0
    mfc::BOOL Open(const char* path, std::uint32_t root);                     // 00697340 (RegCreateKeyExA)
    void Close();                                                             // 006973A0
    void Destruct();                                                          // 006972B0
    // The value `name` of `subkey`, or `defaultValue` when it is missing or of another type.
    int GetInt(const char* subkey, const char* name, int defaultValue);       // 00698610
    int GetDword(const char* subkey, const char* name, int defaultValue);     // 00698570 (same code)
    // A REG_DWORD holding float bits, returned in ST0 (FLD of the float). The default is passed
    // as raw bits, as the original pushes it.
    double GetFloat(const char* subkey, const char* name, std::uint32_t defaultBits);   // 00698750
    // 006988A0. Takes the default CString by value (and destroys it); see GetString below.
    mfc::CString GetStringRaw(const char* subkey, const char* name, RawString defaultValue);

    // The writers: each opens `subkey` (RegCreateKeyExA), sets the value and closes it again.
    // They return 1 on success.
    mfc::BOOL SetDword(const char* subkey, const char* name, int value);          // 00697CA0 (REG_DWORD)
    mfc::BOOL SetInt(const char* subkey, const char* name, int value);            // 00697D40 (same code)
    mfc::BOOL SetFloat(const char* subkey, const char* name, std::uint32_t bits); // 00697E80 (REG_BINARY, 4 bytes)
    // 00697FC0. Takes the value CString by value (and destroys it); see SetString below.
    mfc::BOOL SetStringRaw(const char* subkey, const char* name, RawString value);
};
__declspec(naked) void RegistryKey::Construct() EI_ORIGINAL(0x006972A0)
__declspec(naked) mfc::BOOL RegistryKey::Open(const char*, std::uint32_t) EI_ORIGINAL(0x00697340)
__declspec(naked) void RegistryKey::Close() EI_ORIGINAL(0x006973A0)
__declspec(naked) void RegistryKey::Destruct() EI_ORIGINAL(0x006972B0)
__declspec(naked) int RegistryKey::GetInt(const char*, const char*, int) EI_ORIGINAL(0x00698610)
__declspec(naked) int RegistryKey::GetDword(const char*, const char*, int) EI_ORIGINAL(0x00698570)
__declspec(naked) double RegistryKey::GetFloat(const char*, const char*, std::uint32_t) EI_ORIGINAL(0x00698750)
__declspec(naked) mfc::CString RegistryKey::GetStringRaw(const char*, const char*, RawString) EI_ORIGINAL(0x006988A0)
__declspec(naked) mfc::BOOL RegistryKey::SetDword(const char*, const char*, int) EI_ORIGINAL(0x00697CA0)
__declspec(naked) mfc::BOOL RegistryKey::SetInt(const char*, const char*, int) EI_ORIGINAL(0x00697D40)
__declspec(naked) mfc::BOOL RegistryKey::SetFloat(const char*, const char*, std::uint32_t) EI_ORIGINAL(0x00697E80)
__declspec(naked) mfc::BOOL RegistryKey::SetStringRaw(const char*, const char*, RawString) EI_ORIGINAL(0x00697FC0)

// The string value `name` of `subkey`, or `defaultValue`.
mfc::CString GetString(RegistryKey& key, const char* subkey, const char* name, const mfc::CString& defaultValue) {
    RawString copy;
    new (&copy) mfc::CString(defaultValue);   // the callee destroys its argument
    return key.GetStringRaw(subkey, name, copy);
}

// Writes the string `value` as `name` of `subkey`.
void SetString(RegistryKey& key, const char* subkey, const char* name, const mfc::CString& value) {
    RawString copy;
    new (&copy) mfc::CString(value);   // the callee destroys its argument
    key.SetStringRaw(subkey, name, copy);
}

// CString members used here.
struct Text : mfc::CString {
    int FindChar(int c) const;            // 00700E91 Find(TCHAR)
};
int Text::FindChar(int c) const { return CStringFindCharOriginal(this, nullptr, static_cast<char>(c)); }
const Text& Ops(const mfc::CString& s) { return static_cast<const Text&>(s); }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

constexpr std::uint32_t kCurrentUser = 0x80000001;   // HKEY_CURRENT_USER
constexpr std::uintptr_t kTimes255 = 0x0073C370;     // float 255
constexpr std::uintptr_t kOver255 = 0x0073C36C;      // float 1/255

float FloatAt(const void* p) { return *static_cast<const float*>(p); }
float FloatAt(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

// _ftol(*value * *factor): the product rounded only by the FPU's precision, then truncated by the
// game's _ftol (006E853C).
int FtolProduct(const void* value, std::uintptr_t factor) {
    return static_cast<int>(gamecrt::Ftol(static_cast<double>(FloatAt(value)) * FloatAt(factor)));
}
// _ftol(*value).
int Ftol(const void* value) { return static_cast<int>(gamecrt::Ftol(FloatAt(value))); }
// *out = float(FILD of `value` as an unsigned 64-bit integer, times *factor): the product rounded
// by the FPU's precision, then once to float by FSTP.
void StoreUnsignedProduct(void* out, std::uint32_t value, std::uintptr_t factor) {
    *static_cast<float*>(out) = static_cast<float>(x87::FromUnsigned(value) * FloatAt(factor));
}
// *out = float(FILD of `value` as an unsigned 64-bit integer).
void StoreUnsigned(void* out, std::uint32_t value) { *static_cast<float*>(out) = static_cast<float>(x87::FromUnsigned(value)); }

enum Kind { kInt, kDword, kFloat };

// One value: the getter, the subkey field, the value name's address, the field it goes to (and
// whose value is the default).
struct Value {
    Kind kind;
    std::uint8_t key;
    std::uintptr_t name;
    std::uint16_t field;
};

constexpr Value kVideoValues[] = {
    {kDword, 0x0C, 0x007854C4, 0x038},   // VideoColorDepthInBits
    {kDword, 0x0C, 0x007854AC, 0x03C},   // VideoZBufferDepthInBits
    {kDword, 0x0C, 0x00785498, 0x040},   // VideoRefreshRate
    {kInt, 0x0C, 0x0078548C, 0x044},     // FullScreen
};
// Joystick (+0x48, default 0) comes between these two lists.
constexpr Value kAudioToGeneralValues[] = {
    {kInt, 0x0C, 0x00785470, 0x04C},     // Video
    {kInt, 0x0C, 0x00785478, 0x050},     // Audio
    {kInt, 0x0C, 0x00785460, 0x054},     // AudioStreaming
    {kInt, 0x0C, 0x00785454, 0x058},     // AudioSFX
    {kInt, 0x0C, 0x00785448, 0x05C},     // AudioSpeech
    {kInt, 0x0C, 0x00785438, 0x068},     // Antialiasing
    {kInt, 0x0C, 0x0078542C, 0x060},     // Dithering
    {kInt, 0x0C, 0x00785420, 0x064},     // Specular
    {kInt, 0x0C, 0x00785410, 0x06C},     // TextureQuality
    {kInt, 0x0C, 0x00785404, 0x070},     // Mipmapping
    {kInt, 0x0C, 0x007853F8, 0x074},     // Filtering
    {kInt, 0x0C, 0x007853E0, 0x078},     // PerspectiveCorrection
    {kInt, 0x0C, 0x007853D0, 0x07C},     // ShowParticles
    {kInt, 0x0C, 0x007853B8, 0x080},     // FPSIndependentCursor
    {kInt, 0x0C, 0x007853A0, 0x084},     // AutomaticQuantization
    {kInt, 0x0C, 0x0078538C, 0x088},     // MovieCapturingFPS
    {kFloat, 0x0C, 0x00785380, 0x08C},   // Brightness
    {kFloat, 0x0C, 0x00785374, 0x090},   // Contrast
    {kFloat, 0x0C, 0x0078536C, 0x094},   // Gamma
    {kInt, 0x0C, 0x0078535C, 0x098},     // Reverse Stereo
    {kInt, 0x14, 0x0078534C, 0x09C},     // ShadowQuality
    {kInt, 0x14, 0x00785340, 0x0A0},     // HeroShadow
    {kInt, 0x14, 0x00785334, 0x0A4},     // UnitShadow
    {kInt, 0x14, 0x00785324, 0x0A8},     // BuildingShadow
    {kInt, 0x14, 0x00785318, 0x0AC},     // FloraShadow
    {kInt, 0x14, 0x0078530C, 0x0B0},     // OtherShadow
    {kInt, 0x14, 0x007852FC, 0x0B4},     // LightingFreq
    {kInt, 0x14, 0x007852E8, 0x0B8},     // FloraSpecialLight
    {kFloat, 0x08, 0x007852E0, 0x0BC},   // LOD1
    {kFloat, 0x08, 0x007852D8, 0x0C0},   // LOD2
    {kFloat, 0x08, 0x007852C4, 0x0C4},   // BorderFogDistance
    {kInt, 0x08, 0x007852B0, 0x0C8},     // EnableWaterWaves
    {kInt, 0x08, 0x0078529C, 0x0D0},     // EnableDynamicLights
    {kInt, 0x08, 0x00785288, 0x0CC},     // EnableSelfShadowing
    {kInt, 0x08, 0x007852FC, 0x0D4},     // LightingFreq
    {kInt, 0x08, 0x00785278, 0x0D8},     // ShadowingFreq
    {kInt, 0x08, 0x00785264, 0x0DC},     // EnableFootprints
    {kInt, 0x08, 0x00785250, 0x0E0},     // EnableBloodprints
    {kInt, 0x08, 0x0078523C, 0x0E4},     // EnableFireprints
    {kInt, 0x08, 0x00785220, 0x0E8},     // EnableBloodprintsRelight
    {kInt, 0x00, 0x0078520C, 0x0EC},     // CreateOutputInfo
    {kInt, 0x00, 0x007851FC, 0x0F0},     // NumberOfFrames
};
constexpr Value kCameraValues[] = {
    {kFloat, 0x10, 0x007851CC, 0x0FC},   // CameraMinHeight
    {kFloat, 0x10, 0x007851BC, 0x100},   // CameraMaxHeight
    {kFloat, 0x10, 0x007851A8, 0x104},   // CameraDefaultHeight
    {kFloat, 0x10, 0x00785190, 0x108},   // CameraDefaultXDeploy
    {kFloat, 0x10, 0x00785178, 0x10C},   // CameraDefaultYDeploy
    {kFloat, 0x10, 0x0078515C, 0x110},   // CameraMinDistanceToCarrier
    {kFloat, 0x10, 0x00785140, 0x114},   // CameraMaxDistanceToCarrier
    {kFloat, 0x10, 0x00785120, 0x118},   // CameraMinLimitDistanceToCarrier
    {kFloat, 0x10, 0x00785100, 0x11C},   // CameraMaxLimitDistanceToCarrier
    {kFloat, 0x10, 0x007850E0, 0x120},   // CameraDefaultDistanceToCarrier
    {kFloat, 0x10, 0x00784D7C, 0x1E0},   // NearClipDistance
    {kFloat, 0x10, 0x00784D6C, 0x1E4},   // FarClipDistance
    {kFloat, 0x10, 0x007850C8, 0x1D8},   // CameraDefaultMinPitch
    {kFloat, 0x10, 0x007850B0, 0x1DC},   // CameraDefaultMaxPitch
    {kFloat, 0x10, 0x0078508C, 0x124},   // CameraBriefingMinDistanceToCarrier
    {kFloat, 0x10, 0x00785068, 0x128},   // CameraBriefingMaxDistanceToCarrier
    {kFloat, 0x10, 0x00785040, 0x12C},   // CameraBriefingMinLimitDistanceToCarrier
    {kFloat, 0x10, 0x00785018, 0x130},   // CameraBriefingMaxLimitDistanceToCarrier
    {kFloat, 0x10, 0x00785000, 0x134},   // CameraBriefingMinPitch
    {kFloat, 0x10, 0x00784FE8, 0x138},   // CameraBriefingMaxPitch
    {kInt, 0x10, 0x00784FD0, 0x13C},     // CameraBorderScrollArea
    {kInt, 0x10, 0x00784FAC, 0x140},     // CameraFrameSelectionSensetiveArea
    {kFloat, 0x10, 0x00784F90, 0x144},   // CameraTerrainSensetiveArea
    {kFloat, 0x10, 0x00784F74, 0x148},   // CameraDefaultScrollSpeed
    {kFloat, 0x10, 0x00784F54, 0x14C},   // CameraDefaultScrollPowerMouse
    {kFloat, 0x10, 0x00784F30, 0x150},   // CameraDefaultScrollPowerKeyboard
    {kFloat, 0x10, 0x00784F14, 0x154},   // CameraDefaultRotateSpeed
    {kFloat, 0x10, 0x00784EF4, 0x158},   // CameraDefaultRotatePowerMouse
    {kFloat, 0x10, 0x00784ED0, 0x15C},   // CameraDefaultRotatePowerKeyboard
    {kFloat, 0x10, 0x00784EB8, 0x160},   // CameraDefaultPitchSpeed
    {kFloat, 0x10, 0x00784E98, 0x164},   // CameraDefaultPitchPowerMouse
    {kFloat, 0x10, 0x00784E78, 0x168},   // CameraDefaultPitchPowerKeyboard
    {kFloat, 0x10, 0x00784E60, 0x16C},   // CameraDefaultZoomSpeed
    {kFloat, 0x10, 0x00784E44, 0x170},   // CameraDefaultZoomPowerMouse
    {kFloat, 0x10, 0x00784E24, 0x174},   // CameraDefaultZoomPowerKeyboard
};
constexpr Value kGameValues[] = {
    {kFloat, 0x04, 0x00784D58, 0x1E8},   // DefaultWindForce
    {kInt, 0x04, 0x00784D44, 0x1EC},     // DefaultTimeScale
    {kInt, 0x04, 0x00784D34, 0x1F0},     // UseSWCursors
    {kInt, 0x04, 0x00784D24, 0x1F4},     // SelectionType
    {kFloat, 0x04, 0x00784D18, 0x1F8},   // CornerSize
    {kInt, 0x04, 0x00784D08, 0x1FC},     // DrawVisibleOnly
    {kInt, 0x04, 0x00784CFC, 0x200},     // DrawPath
    {kInt, 0x04, 0x00784CEC, 0x204},     // DrawFlyingHP
    {kInt, 0x04, 0x00784CDC, 0x208},     // ShowTutorial
    {kInt, 0x04, 0x00784CD0, 0x20C},     // Autosave
    {kFloat, 0x04, 0x00784CC0, 0x210},   // ToolTip Time
    {kInt, 0x04, 0x00784CB0, 0x214},     // SwitchFilters
};
constexpr Value kGameValues2[] = {
    {kDword, 0x04, 0x00784C6C, 0x228},   // Keyboard Layout
    {kFloat, 0x04, 0x00784C54, 0x22C},   // ObjectsLightingCoeff
};
constexpr Value kFogValues[] = {
    {kFloat, 0x04, 0x00784B78, 0x260},   // FogNightStartDistance
    {kFloat, 0x04, 0x00784B64, 0x264},   // FogDayStartDistance
};

// One string option: subkey field, value name, field.
struct StringValue {
    std::uint8_t key;
    std::uintptr_t name;
    std::uint16_t field;
};
constexpr StringValue kPathValues[] = {
    {0x18, 0x00785518, 0x020},   // Work path
    {0x18, 0x00785508, 0x024},   // Resources path
    {0x18, 0x007854FC, 0x028},   // CD-ROM Path
};
constexpr StringValue kCdValues[] = {
    {0x04, 0x00784CA4, 0x218},   // CD1 Label
    {0x04, 0x00784C98, 0x21C},   // CD2 Label
    {0x04, 0x00784C8C, 0x220},   // CD3 Label
    {0x04, 0x00784C7C, 0x224},   // Font CharSet
};
constexpr StringValue kDefaultValues[] = {
    {0x04, 0x00784BA8, 0x258},   // DefaultMapName
    {0x04, 0x00784B90, 0x25C},   // DefaultStreamingSound
};

// The lights: r, g, b stored as 0..1 (the registry holds 0..255), then the radius.
constexpr std::uintptr_t kHeroLightNames[] = {0x00784C44, 0x00784C34, 0x00784C24, 0x00784C10};
constexpr std::uintptr_t kQuestLightNames[] = {0x00784C00, 0x00784BF0, 0x00784BE0, 0x00784BCC};

// The camera shortcuts: format (with the shortcut index), field offset in the 0x18-byte shortcut.
constexpr struct {
    std::uintptr_t format;
    std::uint8_t offset;
} kShortcutValues[] = {
    {0x00784E0C, 0x04},   // CameraShortcut %d AxisX
    {0x00784DF4, 0x08},   // CameraShortcut %d AxisY
    {0x00784DDC, 0x0C},   // CameraShortcut %d AxisZ
    {0x00784DC4, 0x10},   // CameraShortcut %d Angle
    {0x00784DA8, 0x00},   // CameraShortcut %d Distance
    {0x00784D90, 0x14},   // CameraShortcut %d Pitch
};

class Loader {
public:
    Loader(char* options, RegistryKey& key) : o_(options), key_(key) {}

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
        case kInt: SetBits(v.field, key_.GetInt(Key(v.key), game::Text(v.name), static_cast<int>(Bits(v.field)))); break;
        case kDword: SetBits(v.field, key_.GetDword(Key(v.key), game::Text(v.name), static_cast<int>(Bits(v.field)))); break;
        case kFloat: SetFloat(v.field, static_cast<float>(key_.GetFloat(Key(v.key), game::Text(v.name), Bits(v.field)))); break;
        }
    }
    template <std::size_t N>
    void Read(const Value (&values)[N]) {
        for (const Value& v : values) Read(v);
    }
    template <std::size_t N>
    void Read(const StringValue (&values)[N]) {
        for (const StringValue& v : values) String(v.field) = GetString(key_, Key(v.key), game::Text(v.name), String(v.field));
    }

    // Stored as -1 / 1 floats, kept in the registry as booleans.
    void ReadCameraFlip(std::size_t offset, std::uintptr_t name) {
        int flipped = Bits(offset) == 0xBF800000u ? 1 : 0;   // the current value is -1
        int value = key_.GetInt(Key(field::kKeyCamera), game::Text(name), flipped);
        SetFloat(offset, static_cast<float>(value ? -1 : 1));
    }

    void ReadCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, game::Text(v.format), i);
                SetFloat(base + v.offset, static_cast<float>(key_.GetFloat(Key(field::kKeyCamera), name, Bits(base + v.offset))));
            }
        }
    }

    // The colour channels are kept in the registry as 0..255 integers.
    void ReadLight(std::size_t offset, const std::uintptr_t (&names)[4]) {
        for (int c = 0; c < 3; ++c) {
            std::size_t f = offset + c * 4;
            int value = key_.GetDword(Key(field::kKeyGame), game::Text(names[c]), FtolProduct(o_ + f, kTimes255));
            StoreUnsignedProduct(o_ + f, static_cast<std::uint32_t>(value), kOver255);
        }
        std::size_t f = offset + 12;
        int radius = key_.GetDword(Key(field::kKeyGame), game::Text(names[3]), Ftol(o_ + f));
        StoreUnsigned(o_ + f, static_cast<std::uint32_t>(radius));
    }

private:
    char* o_;
    RegistryKey& key_;
};

// The writer: the same tables in the same order, each value through the setter matching its
// getter (GetInt -> SetInt, GetDword -> SetDword).
class Saver {
public:
    Saver(char* options, RegistryKey& key) : o_(options), key_(key) {}

    const char* Key(std::size_t offset) const { return *reinterpret_cast<const char* const*>(o_ + offset); }
    std::uint32_t Bits(std::size_t offset) const {
        std::uint32_t v;
        std::memcpy(&v, o_ + offset, 4);
        return v;
    }
    const mfc::CString& String(std::size_t offset) const { return *reinterpret_cast<const mfc::CString*>(o_ + offset); }

    void Write(const Value& v) {
        switch (v.kind) {
        case kInt: key_.SetInt(Key(v.key), game::Text(v.name), static_cast<int>(Bits(v.field))); break;
        case kDword: key_.SetDword(Key(v.key), game::Text(v.name), static_cast<int>(Bits(v.field))); break;
        case kFloat: key_.SetFloat(Key(v.key), game::Text(v.name), Bits(v.field)); break;
        }
    }
    template <std::size_t N>
    void Write(const Value (&values)[N]) {
        for (const Value& v : values) Write(v);
    }
    template <std::size_t N>
    void Write(const StringValue (&values)[N]) {
        for (const StringValue& v : values) SetString(key_, Key(v.key), game::Text(v.name), String(v.field));
    }

    // 1 when the stored float is exactly -1 (compared as bits).
    void WriteCameraFlip(std::size_t offset, std::uintptr_t name) {
        key_.SetInt(Key(field::kKeyCamera), game::Text(name), Bits(offset) == 0xBF800000u ? 1 : 0);
    }

    void WriteCameraShortcuts() {
        char name[0x400];
        for (unsigned i = 0; i < 4; ++i) {
            std::size_t base = field::kCameraShortcuts + i * 0x18;
            for (const auto& v : kShortcutValues) {
                gamecrt::sprintf(name, game::Text(v.format), i);
                key_.SetFloat(Key(field::kKeyCamera), name, Bits(base + v.offset));
            }
        }
    }

    // The colour channels as _ftol(value * 255), the radius as _ftol(radius).
    void WriteLight(std::size_t offset, const std::uintptr_t (&names)[4]) {
        for (int c = 0; c < 3; ++c)
            key_.SetDword(Key(field::kKeyGame), game::Text(names[c]), FtolProduct(o_ + offset + c * 4, kTimes255));
        key_.SetDword(Key(field::kKeyGame), game::Text(names[3]), Ftol(o_ + offset + 12));
    }

private:
    char* o_;
    RegistryKey& key_;
};

} // namespace

void GameOptions::LoadFromRegistry(const char* path) {
    char* o = reinterpret_cast<char*>(this);
    mfc::CString resolution;   // the original's local is constructed first, read later
    RegistryKey key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Loader l(o, key);

    l.Read(kPathValues);
    l.Read(Value{kInt, 0x0C, 0x007854EC, 0x02C});   // DrawingTarget
    {
        // VideoResolution goes through the local string the original built first.
        resolution = GetString(key, l.Key(field::kKeyVideo), game::Text(0x007854DC), resolution);
        int x = Ops(resolution).FindChar('x');
        if (x != -1) {
            l.SetBits(field::kResolutionX, static_cast<std::uint32_t>(GameAtoi(resolution.Left(x))));
            l.SetBits(field::kResolutionY,
                      static_cast<std::uint32_t>(GameAtoi(Ops(resolution).Right(Length(resolution) - x - 1))));
        }
    }
    l.Read(kVideoValues);
    l.SetBits(0x048, static_cast<std::uint32_t>(key.GetInt(l.Key(field::kKeyVideo), game::Text(0x00785480), 0)));   // Joystick
    l.Read(kAudioToGeneralValues);
    l.ReadCameraFlip(field::kCameraFlipX, 0x007851EC);   // CameraFlipXAxis
    l.ReadCameraFlip(field::kCameraFlipY, 0x007851DC);   // CameraFlipYAxis
    l.Read(kCameraValues);
    l.ReadCameraShortcuts();
    l.Read(kGameValues);
    l.Read(kCdValues);
    l.Read(kGameValues2);
    l.ReadLight(field::kHeroLight, kHeroLightNames);
    l.ReadLight(field::kQuestLight, kQuestLightNames);
    int difficulty = key.GetDword(l.Key(field::kKeyGame), game::Text(0x00784BB8), static_cast<int>(l.Bits(field::kDifficulty + 4)));
    l.SetBits(field::kDifficulty + 4, static_cast<std::uint32_t>(difficulty));
    l.SetBits(field::kDifficulty, static_cast<std::uint32_t>(difficulty));
    l.Read(kDefaultValues);
    l.Read(kFogValues);
    l.String(field::kMasterServerName) =
        GetString(key, l.Key(field::kKeyNetwork), game::Text(0x00784B50), l.String(field::kMasterServerName));
    key.Close();
    key.Destruct();
}
EI_REPLACE_METHOD(0x0045B210, GameOptions::LoadFromRegistry);

void GameOptions::SaveToRegistry(const char* path) {
    char* o = reinterpret_cast<char*>(this);
    mfc::CString resolution;
    RegistryKey key;
    key.Construct();
    key.Open(path, kCurrentUser);
    Saver s(o, key);

    s.Write(kPathValues);
    s.Write(Value{kInt, 0x0C, 0x007854EC, 0x02C});   // DrawingTarget
    resolution.Format(game::Text(0x00785524), static_cast<int>(s.Bits(field::kResolutionX)),   // "%dx%d"
                      static_cast<int>(s.Bits(field::kResolutionY)));
    SetString(key, s.Key(field::kKeyVideo), game::Text(0x007854DC), resolution);   // VideoResolution
    s.Write(kVideoValues);
    key.SetInt(s.Key(field::kKeyVideo), game::Text(0x00785480), 0);   // Joystick
    s.Write(kAudioToGeneralValues);
    s.WriteCameraFlip(field::kCameraFlipX, 0x007851EC);   // CameraFlipXAxis
    s.WriteCameraFlip(field::kCameraFlipY, 0x007851DC);   // CameraFlipYAxis
    s.Write(kCameraValues);
    s.WriteCameraShortcuts();
    s.Write(kGameValues);
    s.Write(kCdValues);
    s.Write(kGameValues2);
    s.WriteLight(field::kHeroLight, kHeroLightNames);
    s.WriteLight(field::kQuestLight, kQuestLightNames);
    key.SetDword(s.Key(field::kKeyGame), game::Text(0x00784BB8), static_cast<int>(s.Bits(field::kDifficulty + 4)));
    s.Write(kDefaultValues);
    s.Write(kFogValues);
    // EI_Plugin's NewMaster NOPs these 42 bytes (0045D330): the Master Server Name is not saved.
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        SetString(key, s.Key(field::kKeyNetwork), game::Text(0x00784B50), s.String(field::kMasterServerName));
    key.Close();
    key.Destruct();
}
EI_REPLACE_METHOD(0x0045C560, GameOptions::SaveToRegistry);

} // namespace options
