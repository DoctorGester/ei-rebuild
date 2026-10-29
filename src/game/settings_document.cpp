// The settings file's text and its defaults (no registry, no game): Document, its reg-export-style lines, JoinPath,
// DefaultDocument. See settings_file.h; tests/vectors/replay_settings.cpp checks them without the game.
#include "settings_file.h"
#include "lib/bytes.h"

#include <bit>
#include <string>
#include <windows.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace settings_file {
namespace {

bool Same(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

// ---- The text ----

std::string Quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + '"';
}

// A string the quoted form holds: one terminator at its end, no other control character.
bool PlainString(const std::string& data) {
    if (data.empty() || data.back() != '\0') return false;
    for (std::size_t i = 0; i + 1 < data.size(); ++i)
        if (static_cast<unsigned char>(data[i]) < 0x20) return false;
    return true;
}

// The 4 bytes as "float:<value>" when the text reads back to the same bits (not for NaNs and infinities).
bool FloatText(const std::string& data, std::string* out) {
    if (data.size() != 4) return false;
    std::uint32_t bits;
    lib::LoadBytes(bits, data.data());
    if ((bits & 0x7F800000u) == 0x7F800000u) return false;
    float value;
    value = std::bit_cast<float>(bits);
    std::array<char, 32> text;
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    if (written.ec != std::errc()) return false;
    float back = 0;
    const auto read = std::from_chars(text.data(), written.ptr, back);
    std::uint32_t backBits;
    backBits = std::bit_cast<std::uint32_t>(back);
    if (read.ec != std::errc() || read.ptr != written.ptr || backBits != bits) return false;
    *out = "float:" + std::string(text.data(), written.ptr);
    return true;
}

// `value` as `digits` lower-case hex digits.
void AppendHex(std::string& out, std::uint32_t value, int digits) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) out += kDigits[(value >> shift) & 0xF];
}

std::string ValueText(const Value& v) {
    std::string out;
    if (v.type == REG_DWORD && v.data.size() == 4) {
        std::uint32_t dword;
        lib::LoadBytes(dword, v.data.data());
        out = "dword:";
        AppendHex(out, dword, 8);
        return out;
    }
    if (v.type == REG_SZ && PlainString(v.data)) return Quoted(v.data.substr(0, v.data.size() - 1));
    if (v.type == REG_BINARY && FloatText(v.data, &out)) return out;
    if (v.type == REG_BINARY) {
        out = "hex:";
    } else {
        std::array<char, 16> type;
        out = "hex(" + std::string(type.data(), std::to_chars(type.data(), type.data() + type.size(), v.type, 16).ptr) + "):";
    }
    for (std::size_t i = 0; i < v.data.size(); ++i) {
        if (i) out += ',';
        AppendHex(out, static_cast<unsigned char>(v.data[i]), 2);
    }
    return out;
}

// The quoted text at `p` (\\ and \" escaped); `p` moves past its closing quote.
bool ParseQuoted(const char*& p, std::string* out) {
    if (*p != '"') return false;
    ++p;
    out->clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        *out += *p++;
    }
    if (*p != '"') return false;
    ++p;
    return true;
}

bool ParseHex(const char* p, std::string* out) {
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        unsigned byte = 0;
        const auto read = std::from_chars(p, p + 2, byte, 16);
        if (read.ec != std::errc() || read.ptr != p + 2) return false;
        *out += static_cast<char>(byte);
        p += 2;
    }
    return true;
}

bool StartsWith(const char* p, const char* prefix) { return _strnicmp(p, prefix, std::strlen(prefix)) == 0; }

bool ParseValue(const char* p, Value* v) {
    const char* end = p + std::strlen(p);
    if (*p == '"') {
        if (!ParseQuoted(p, &v->data) || *p) return false;
        v->type = REG_SZ;
        v->data += '\0';
        return true;
    }
    if (StartsWith(p, "dword:")) {
        std::uint32_t dword = 0;
        const auto read = std::from_chars(p + 6, end, dword, 16);
        if (read.ec != std::errc() || read.ptr != end || end - (p + 6) > 8) return false;
        v->type = REG_DWORD;
        v->data.assign(reinterpret_cast<const char*>(&dword), 4);
        return true;
    }
    if (StartsWith(p, "float:")) {
        float value = 0;
        const auto read = std::from_chars(p + 6, end, value);
        if (read.ec != std::errc() || read.ptr != end) return false;
        v->type = REG_BINARY;
        v->data.assign(reinterpret_cast<const char*>(&value), 4);
        return true;
    }
    if (StartsWith(p, "hex:")) {
        v->type = REG_BINARY;
        return ParseHex(p + 4, &v->data);
    }
    if (StartsWith(p, "hex(")) {
        const char* close = std::strchr(p, ')');
        if (!close || close[1] != ':') return false;
        const auto read = std::from_chars(p + 4, close, v->type, 16);
        if (read.ec != std::errc() || read.ptr != close) return false;
        return ParseHex(close + 2, &v->data);
    }
    return false;
}

constexpr const char* kHeader =
    "; Evil Islands' settings for ei_standalone.exe, in place of its registry keys under HKEY_CURRENT_USER.\r\n"
    "; [key below HKEY_CURRENT_USER], then \"name\"=value: dword:<hex> (REG_DWORD), \"text\" (REG_SZ, \\\\ and \\\"\r\n"
    "; escaped), float:<number> (a 4-byte REG_BINARY float), hex:<bytes> (REG_BINARY), hex(<type>):<bytes>.\r\n"
    "; @ is a key's default value. The game rewrites this file when it saves its options.\r\n";

std::string Dword(std::uint32_t value) { return std::string(reinterpret_cast<const char*>(&value), sizeof(value)); }
std::string Real(float value) { return Dword(std::bit_cast<std::uint32_t>(value)); }
std::string Terminated(const char* text) { return std::string(text, std::strlen(text) + 1); }

// What a game install writes and the game's own defaults lack (the original's installer and Rebirth's agree; the
// game's defaults are placeholders: a camera turning about 7 and pitching about 700 times as fast, objects lit half
// as bright). The camera's whole section:
struct InstallFloat {
    const char* name;
    float value;
};
constexpr InstallFloat kInstallCamera[] = {
    {"CameraMinHeight", 3.0f},
    {"CameraMaxHeight", 70.0f},
    {"CameraDefaultHeight", 2.0f},
    {"CameraDefaultXDeploy", 30.0f},
    {"CameraDefaultYDeploy", 30.0f},
    {"CameraMinDistanceToCarrier", 10.0f},
    {"CameraMaxDistanceToCarrier", 50.0f},
    {"CameraMinLimitDistanceToCarrier", 3.0f},
    {"CameraMaxLimitDistanceToCarrier", 70.0f},
    {"CameraDefaultDistanceToCarrier", 30.0f},
    {"NearClipDistance", 2.0f},
    {"FarClipDistance", 100.0f},
    {"CameraDefaultMinPitch", 30.0f},
    {"CameraDefaultMaxPitch", 60.0f},
    {"CameraBriefingMinDistanceToCarrier", 6.0f},
    {"CameraBriefingMaxDistanceToCarrier", 27.0f},
    {"CameraBriefingMinLimitDistanceToCarrier", 3.0f},
    {"CameraBriefingMaxLimitDistanceToCarrier", 30.0f},
    {"CameraBriefingMinPitch", 40.0f},
    {"CameraBriefingMaxPitch", 60.0f},
    {"CameraTerrainSensetiveArea", 10.0f},
    {"CameraDefaultScrollSpeed", 1.5f},
    {"CameraDefaultScrollPowerMouse", 1.0f},
    {"CameraDefaultScrollPowerKeyboard", 0.46f},
    {"CameraDefaultRotateSpeed", 0.15f},
    {"CameraDefaultRotatePowerMouse", 1.0f},
    {"CameraDefaultRotatePowerKeyboard", 0.46f},
    {"CameraDefaultPitchSpeed", 0.0015f},
    {"CameraDefaultPitchPowerMouse", 1.0f},
    {"CameraDefaultPitchPowerKeyboard", 0.46f},
    {"CameraDefaultZoomSpeed", 2.0f},
    {"CameraDefaultZoomPowerMouse", 1.0f},
    {"CameraDefaultZoomPowerKeyboard", 0.46f},
};
// Each of the four shortcuts: the default distance, looking along Z, 45 degrees of pitch.
constexpr InstallFloat kInstallShortcut[] = {{"AxisX", 0.0f}, {"AxisY", 0.0f}, {"AxisZ", 1.0f},
                                             {"Angle", 0.0f}, {"Distance", 30.0f}, {"Pitch", 45.0f}};
// The game settings that differ from the game's own: hardware cursors, the selection type, the objects' lighting,
// the hero's and the quest light (R, G, B of 255, radius), the default map and music.
struct InstallDword {
    const char* name;
    std::uint32_t value;
};
constexpr InstallDword kInstallGameDwords[] = {
    {"UseSWCursors", 0},       {"SelectionType", 1},    {"Hero Light R", 216},   {"Hero Light G", 216},
    {"Hero Light B", 116},     {"Hero Light Radius", 5}, {"Quest Light R", 120}, {"Quest Light G", 72},
    {"Quest Light B", 120},    {"Quest Light Radius", 5},
};

} // namespace


Key* Document::FindKey(const std::string& path) {
    for (Key& key : keys)
        if (Same(key.path, path)) return &key;
    return nullptr;
}

Key& Document::AddKey(const std::string& path) {
    if (Key* key = FindKey(path)) return *key;
    keys.push_back(Key{path, {}});
    return keys.back();
}

Value* Document::FindValue(Key& key, const std::string& name) {
    for (Value& value : key.values)
        if (Same(value.name, name)) return &value;
    return nullptr;
}

void Document::Set(const std::string& path, const std::string& name, unsigned long type, std::string data) {
    Key& key = AddKey(path);
    if (Value* value = FindValue(key, name)) {
        value->type = type;
        value->data = std::move(data);
        return;
    }
    key.values.push_back(Value{name, type, std::move(data)});
}

std::string Document::Text() const {
    std::string out = kHeader;
    for (const Key& key : keys) {
        out += "\r\n[" + key.path + "]\r\n";
        for (const Value& value : key.values)
            out += (value.name.empty() ? std::string("@") : Quoted(value.name)) + "=" + ValueText(value) + "\r\n";
    }
    return out;
}

void Document::Parse(const std::string& text, std::vector<std::string>* problems) {
    Key* key = nullptr;
    std::string line;
    int number = 0;
    for (std::size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0; at < text.size();) {   // past a UTF-8 BOM
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(at, end - at);
        at = end + 1;
        ++number;
        while (!part.empty() && (part.back() == '\r' || part.back() == ' ' || part.back() == '\t')) part.pop_back();
        const std::size_t first = part.find_first_not_of(" \t");
        part = first == std::string::npos ? std::string() : part.substr(first);
        // Hex bytes continue on the next line after a backslash, as reg export writes them.
        if (!part.empty() && part.back() == '\\') {
            part.pop_back();
            line += part;
            continue;
        }
        line += part;
        if (line.empty() || line[0] == ';') {
            line.clear();
            continue;
        }
        bool ok = true;
        if (line[0] == '[' && line.back() == ']') {
            key = &AddKey(JoinPath("", line.substr(1, line.size() - 2).c_str()));
        } else if (key) {
            const char* p = line.c_str();
            Value value;
            if (*p == '@') {
                ++p;
            } else {
                ok = ParseQuoted(p, &value.name);
            }
            while (ok && (*p == ' ' || *p == '\t')) ++p;
            ok = ok && *p++ == '=';
            while (ok && (*p == ' ' || *p == '\t')) ++p;
            ok = ok && ParseValue(p, &value);
            if (ok) Set(key->path, value.name, value.type, std::move(value.data));
        } else {
            ok = false;
        }
        if (!ok) problems->push_back("line " + std::to_string(number) + " not understood: " + line);
        line.clear();
    }
}

std::string JoinPath(const char* path, const char* sub) {
    std::string out;
    for (const char* part : {path, sub}) {
        for (const char* p = part; p && *p;) {
            const char* end = std::strchr(p, '\\');
            if (!end) end = p + std::strlen(p);
            if (end != p) {
                if (!out.empty()) out += '\\';
                out.append(p, end);
            }
            p = *end ? end + 1 : end;
        }
    }
    return out;
}

// ---- The defaults ----

Document DefaultDocument(int width, int height) {
    Document document;
    const std::string resolution = std::to_string(width) + "x" + std::to_string(height) + std::string(1, '\0');
    for (const char* tree : kOptionTrees) {
        const std::string general = JoinPath(tree, "General Settings");
        document.Set(general, "VideoResolution", REG_SZ, resolution);
        document.Set(general, "FullScreen", REG_DWORD, Dword(1));
        document.Set(general, "VideoColorDepthInBits", REG_DWORD, Dword(32));
        document.Set(general, "Audio", REG_DWORD, Dword(1));
        for (const char* volume : {"AudioStreaming", "AudioSFX", "AudioSpeech"}) document.Set(general, volume, REG_DWORD, Dword(100));
        document.Set(general, "Antialiasing", REG_DWORD, Dword(1));
        document.Set(general, "Mipmapping", REG_DWORD, Dword(1));
        document.Set(general, "Filtering", REG_DWORD, Dword(2));        // trilinear
        document.Set(general, "TextureQuality", REG_DWORD, Dword(0));   // no mip level dropped (texture.cpp)
        document.Set(general, "ShowParticles", REG_DWORD, Dword(1));
        const std::string shadows = JoinPath(tree, "Shadow Settings");
        document.Set(shadows, "ShadowQuality", REG_DWORD, Dword(2));
        for (const char* shadow : {"HeroShadow", "UnitShadow", "BuildingShadow", "FloraShadow", "OtherShadow"})
            document.Set(shadows, shadow, REG_DWORD, Dword(1));
        // Rebirth's starter (EIStarter.exe 1.46) writes these distances and water waves on for its best landscape
        // detail, 1000 for its most frequent relighting.
        const std::string terrain = JoinPath(tree, "Terrain Settings");
        document.Set(terrain, "LOD1", REG_BINARY, Real(90.0f));
        document.Set(terrain, "LOD2", REG_BINARY, Real(130.0f));
        document.Set(terrain, "LightingFreq", REG_DWORD, Dword(1000));
        for (const char* feature : {"EnableWaterWaves", "EnableDynamicLights", "EnableSelfShadowing", "EnableFootprints",
                                    "EnableBloodprints", "EnableFireprints"})
            document.Set(terrain, feature, REG_DWORD, Dword(1));
        // The install's camera and game settings.
        const std::string camera = JoinPath(tree, "Camera Settings");
        document.Set(camera, "CameraFlipXAxis", REG_DWORD, Dword(0));
        document.Set(camera, "CameraFlipYAxis", REG_DWORD, Dword(0));
        for (const InstallFloat& value : kInstallCamera) document.Set(camera, value.name, REG_BINARY, Real(value.value));
        document.Set(camera, "CameraBorderScrollArea", REG_DWORD, Dword(1));
        document.Set(camera, "CameraFrameSelectionSensetiveArea", REG_DWORD, Dword(30));
        for (int shortcut = 0; shortcut < 4; ++shortcut)
            for (const InstallFloat& value : kInstallShortcut)
                document.Set(camera, "CameraShortcut " + std::to_string(shortcut) + " " + value.name, REG_BINARY, Real(value.value));
        const std::string game = JoinPath(tree, "Game Specific Settings");
        for (const InstallDword& value : kInstallGameDwords) document.Set(game, value.name, REG_DWORD, Dword(value.value));
        document.Set(game, "ObjectsLightingCoeff", REG_BINARY, Real(2.0f));
        document.Set(game, "DefaultMapName", REG_SZ, Terminated("Zone1.mpr"));
        document.Set(game, "DefaultStreamingSound", REG_SZ, Terminated("melody.mp3"));
        document.Set(JoinPath(tree, "Network Settings"), "Master Server Name", REG_SZ, Terminated("a3master.nival.com"));
    }
    // The renderer's (device_object.cpp).
    constexpr const char* kStandalone = "Software\\Nival Interactive\\EvilIslands\\Standalone";
    document.Set(kStandalone, "VSync", REG_DWORD, Dword(1));
    document.Set(kStandalone, "Msaa", REG_DWORD, Dword(8));
    document.Set(kStandalone, "Anisotropy", REG_DWORD, Dword(16));
    // Rebirth's starter switches (ui_widescreen.cpp, world_client_draw.cpp).
    constexpr const char* kStarter = "Software\\Gipat.Ru\\EI_Starter\\Settings";
    document.Set(kStarter, "WideScreen", REG_DWORD, Dword(1));
    document.Set(kStarter, "ObjectsDrawRadius", REG_DWORD, Dword(5376));
    document.Set(kStarter, "LandscapeDrawRadius", REG_DWORD, Dword(5376));
    return document;
}

} // namespace settings_file
