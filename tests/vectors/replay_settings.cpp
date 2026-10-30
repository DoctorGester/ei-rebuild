// The standalone's settings file without the game: Document::Text / Parse round trips of every kind of value, the
// format's rules (case, quotes, comments, bad lines, a file of another format) and the defaults a missing file is
// written with. Not recorded vectors: ei_vectors runs these after the replays and counts their failures
// (replay_main.cpp). The file save (.tmp, then moved) and the defaults in a running game are the harness scenarios
// settings_save and settings_defaults (tests/scenarios).
#include "../../src/game/settings.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace {

using settings::Document;

int g_checks = 0;
int g_failures = 0;

void Fail(int line, const char* what, const std::string& detail) {
    ++g_failures;
    if (g_failures <= 20) std::printf("  settings file check (line %d): %s%s%s\n", line, what, detail.empty() ? "" : ": ", detail.c_str());
}

#define CHECK(cond)                                  \
    do {                                             \
        ++g_checks;                                  \
        if (!(cond)) Fail(__LINE__, #cond, "");      \
    } while (0)
#define CHECK_MSG(cond, detail)                      \
    do {                                             \
        ++g_checks;                                  \
        if (!(cond)) Fail(__LINE__, #cond, (detail)); \
    } while (0)

// The document's text read back.
Document RoundTrip(const Document& document, std::vector<std::string>* problems = nullptr) {
    std::vector<std::string> own;
    Document back;
    back.Parse(document.Text(), problems ? problems : &own);
    return back;
}

std::string Value(const Document& document, const char* section, const char* name) {
    const std::string* value = document.Find(section, name);
    return value ? *value : std::string("(missing)");
}

void Ints() {
    for (int value : {0, 1, -1, 42, 1033, 2147483647, -2147483647 - 1}) {
        int back = 7;
        CHECK_MSG(settings::ParseInt(settings::IntText(value), &back) && back == value, settings::IntText(value));
    }
    int value = 0;
    CHECK(settings::ParseInt("4294967295", &value) && value == -1);   // a DWORD's bits
    CHECK(settings::ParseInt("+5", &value) && value == 5);
    CHECK(settings::ParseInt("true", &value) && value == 1 && settings::ParseInt("Off", &value) && value == 0);
    for (const char* bad : {"", "-", "1.5", "0x10", "12a", "4294967296", "-2147483649", " 1"}) {
        value = 7;
        CHECK_MSG(!settings::ParseInt(bad, &value) && value == 7, bad);
    }
}

void Bools() {
    bool value = false;
    for (const char* yes : {"true", "TRUE", "yes", "on", "1", "2", "-1"}) CHECK_MSG(settings::ParseBool(yes, &value) && value, yes);
    for (const char* no : {"false", "No", "off", "0"}) CHECK_MSG(settings::ParseBool(no, &value) && !value, no);
    value = true;
    CHECK(!settings::ParseBool("maybe", &value) && value);
    CHECK(settings::BoolText(true) == "true" && settings::BoolText(false) == "false");
}

// Every float reads back to its bits (NaNs as a NaN): a sweep of bit patterns plus every exponent.
void Floats() {
    std::uint32_t state = 12345;
    auto check = [](std::uint32_t bits) {
        const float value = std::bit_cast<float>(bits);
        const std::string text = settings::FloatText(value);
        float back = 0;
        const bool read = settings::ParseFloat(text, &back);
        const bool same = std::isnan(value) ? std::isnan(back) : std::bit_cast<std::uint32_t>(back) == bits;
        CHECK_MSG(read && same, text);
    };
    for (int i = 0; i < 20000; ++i) {
        state = state * 1664525u + 1013904223u;
        check(state);
    }
    for (std::uint32_t exponent = 0; exponent < 256; ++exponent) check(exponent << 23 | 0x2AAAAAu);
    for (std::uint32_t bits : {0u, 0x80000000u, 1u, 0x3F800000u, 0xBF800000u, 0x7F800000u, 0xFF800000u}) check(bits);
    CHECK(settings::FloatText(0.46f) == "0.46" && settings::FloatText(90.0f) == "90" && settings::FloatText(-0.0f) == "-0");
    float value = 0;
    CHECK(settings::ParseFloat("3", &value) && value == 3.0f);
    CHECK(settings::ParseFloat("+0.5", &value) && value == 0.5f);
    CHECK(!settings::ParseFloat("", &value) && !settings::ParseFloat("1.5x", &value) && !settings::ParseFloat("true", &value));
}

// Text values, quoted when spaces or a quote at an end would be lost.
void Strings() {
    const char* values[] = {"plain", "", "C:\\Probe Dir\\\"quoted\"\\end\\", "  two spaces around  ", " lead", "trail\t",
                            "\"in quotes\"", "\"", "a\"", "\"a", "a = b ; c # d", "[not a section]", "; not a comment",
                            "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"};
    Document document;
    for (int i = 0; i < static_cast<int>(std::size(values)); ++i) document.Set("text", ("v" + std::to_string(i)).c_str(), values[i]);
    const Document back = RoundTrip(document);
    for (int i = 0; i < static_cast<int>(std::size(values)); ++i)
        CHECK_MSG(Value(back, "text", ("v" + std::to_string(i)).c_str()) == values[i], values[i]);
    // A line break, which no line holds, becomes a space.
    document.Set("text", "lines", "one\r\ntwo");
    CHECK(Value(RoundTrip(document), "text", "lines") == "one  two");
    // Spaces around the '=' and the line's ends are not the value's.
    Document parsed;
    std::vector<std::string> problems;
    parsed.Parse("[a]\r\n  name\t=\t value  \r\nempty =\r\nequals==x\r\n", &problems);
    CHECK(problems.empty() && Value(parsed, "a", "name") == "value" && Value(parsed, "a", "empty").empty() &&
          Value(parsed, "a", "equals") == "=x");
}

// Sections and names without case, the first spelling kept; a value set again stays where it was.
void Names() {
    Document document;
    document.Set("Video", "FullScreen", "true");
    document.Set("video", "FULLSCREEN", "false");
    document.Set("VIDEO", "Other", "1");
    CHECK(document.sections.size() == 1 && document.sections[0].name == "Video" && document.sections[0].lines.size() == 2);
    CHECK(document.sections[0].lines[0].name == "FullScreen" && document.sections[0].lines[0].value == "false");
    CHECK(document.Find("VIDEO", "fullscreen") && !document.Find("video", "full screen") && !document.Find("audio", "Other"));
    CHECK(document.Values() == 2);
    // A section met twice in a file is one section.
    Document parsed;
    std::vector<std::string> problems;
    parsed.Parse("[a]\nx=1\n[b]\ny=2\n[A]\nx=3\nz=4\n", &problems);
    CHECK(problems.empty() && parsed.sections.size() == 2 && Value(parsed, "a", "x") == "3" && Value(parsed, "a", "z") == "4");
}

// Comments are kept where they are; the lines before the first section stay first.
void Comments() {
    std::vector<std::string> problems;
    Document document;
    document.Parse("\xEF\xBB\xBF; top\r\n# also top\r\n\r\n[a]\r\n; in a\r\nx = 1\r\n\r\n[b]\r\ny = 2\r\n; end of b\r\n", &problems);
    CHECK(problems.empty());
    CHECK(document.Text() == "; top\r\n# also top\r\n\r\n[a]\r\n; in a\r\nx = 1\r\n\r\n[b]\r\ny = 2\r\n; end of b\r\n");
    document.Comment("", "added");
    document.Comment("c", "new section");
    CHECK(document.Text() == "; top\r\n# also top\r\n; added\r\n\r\n[a]\r\n; in a\r\nx = 1\r\n\r\n[b]\r\ny = 2\r\n; end of b\r\n"
                             "\r\n[c]\r\n; new section\r\n");
    // A document without lines before its first section starts with it.
    Document plain;
    plain.Set("a", "x", "1");
    CHECK(plain.Text() == "[a]\r\nx = 1\r\n");
    // Line breaks: \n alone, and a last line without one.
    Document unix;
    unix.Parse("[a]\nx = 1\ny = 2", &problems);
    CHECK(problems.empty() && Value(unix, "a", "y") == "2");
}

// Lines not understood are reported and skipped; below a bad section every line is skipped.
void BadLines() {
    std::vector<std::string> problems;
    Document document;
    document.Parse("x = before any section\n[good]\nno equals sign\n= no name\nok = 1\n[bad\\name]\nskipped = 1\n; skipped\n"
                   "[]\nalso = skipped\n[good2]\nfine = 2\n",
                   &problems);
    CHECK_MSG(problems.size() == 5, std::to_string(problems.size()));
    CHECK(Value(document, "good", "ok") == "1" && Value(document, "good2", "fine") == "2" && !document.Find("bad\\name", "skipped"));
    CHECK(document.Values() == 2);
    // The registry-shaped file of earlier builds holds no setting: the store writes the defaults over it.
    Document old;
    problems.clear();
    old.Parse("; Evil Islands' settings for ei-rebuild.exe, in place of its registry keys under HKEY_CURRENT_USER.\r\n"
              "\r\n[Software\\Nival Interactive\\EvilIslands\\General Settings]\r\n\"FullScreen\"=dword:00000001\r\n"
              "\"VideoResolution\"=\"1920x1080\"\r\n\r\n[Software\\Gipat.Ru\\EI_Starter\\Settings]\r\n\"WideScreen\"=dword:00000001\r\n",
              &problems);
    CHECK(old.Values() == 0 && problems.size() == 2);
}

void Defaults() {
    const Document document = settings::Defaults(2560, 1440);
    auto is = [&](const char* section, const char* name, const char* value) {
        CHECK_MSG(Value(document, section, name) == value, std::string(section) + " " + name + " = " + Value(document, section, name));
    };
    is("video", "VideoResolution", "2560x1440");
    is("video", "FullScreen", "true");
    is("video", "VideoColorDepthInBits", "32");
    is("video", "Antialiasing", "true");
    is("video", "Mipmapping", "true");
    is("video", "Filtering", "2");
    is("video", "TextureQuality", "0");
    is("video", "ShowParticles", "true");
    is("sound", "Audio", "true");
    for (const char* volume : {"AudioStreaming", "AudioSFX", "AudioSpeech"}) is("sound", volume, "100");
    is("shadows", "ShadowQuality", "2");
    for (const char* shadow : {"HeroShadow", "UnitShadow", "BuildingShadow", "FloraShadow", "OtherShadow"}) is("shadows", shadow, "true");
    is("terrain", "LightingFreq", "1000");
    for (const char* feature : {"EnableWaterWaves", "EnableDynamicLights", "EnableSelfShadowing", "EnableFootprints",
                                "EnableBloodprints", "EnableFireprints"})
        is("terrain", feature, "true");
    CHECK(!document.Find("terrain", "LOD1") && !document.Find("terrain", "LOD2"));
    is("camera", "CameraFlipXAxis", "false");
    is("camera", "CameraMinHeight", "3");
    is("camera", "CameraDefaultRotateSpeed", "0.15");
    is("camera", "CameraDefaultPitchSpeed", "0.0015");
    is("camera", "CameraDefaultZoomPowerKeyboard", "0.46");
    is("camera", "CameraFrameSelectionSensetiveArea", "30");
    is("camera", "CameraShortcut 3 Pitch", "45");
    is("camera", "CameraShortcut 0 AxisZ", "1");
    is("game", "UseSWCursors", "false");
    is("game", "SelectionType", "1");
    is("game", "Hero Light B", "116");
    is("game", "Quest Light G", "72");
    is("game", "ObjectsLightingCoeff", "2");
    is("game", "DefaultMapName", "Zone1.mpr");
    is("game", "DefaultStreamingSound", "melody.mp3");
    is("network", "Master Server Name", "a3master.nival.com");
    is("renderer", "VSync", "true");
    is("renderer", "Msaa", "8");
    is("renderer", "Anisotropy", "16");
    is("renderer", "PerfOverlay", "false");
    CHECK(!document.Find("rebirth", "WideScreen"));   // no switch: the layout follows the window
    is("rebirth", "ObjectsDrawRadius", "5376");
    is("rebirth", "LandscapeDrawRadius", "5376");
    // Left out: the game's and Rebirth's own defaults.
    CHECK(!document.Find("paths", "Work path") && !document.Find("game", "ShowTutorial") && !document.Find("rebirth", "AddonPath"));
    // The text reads back to the same document, comments included, with no problem.
    std::vector<std::string> problems;
    const Document back = RoundTrip(document, &problems);
    CHECK(problems.empty() && back.Text() == document.Text() && back.Values() == document.Values());
    // Another desktop changes the resolution only.
    const Document other = settings::Defaults(1920, 1080);
    CHECK(Value(other, "video", "VideoResolution") == "1920x1080" && other.Values() == document.Values());
}

} // namespace

// replay_main.cpp: the number of failed checks.
int RunSettingsFileChecks() {
    g_checks = g_failures = 0;
    Ints();
    Bools();
    Floats();
    Strings();
    Names();
    Comments();
    BadLines();
    Defaults();
    std::printf("ei_vectors: settings file: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
