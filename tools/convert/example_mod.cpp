// ei-convert --example-mod: example_mod.h. What each made part is for: docs/modding.md, "The example mod"; the
// scenario that plays it: tests/scenarios/mod_example.txt.
#include "example_mod.h"

#include "convert_mobs.h"
#include "convert_scripts.h"

#include "engine/data/resource.h"
#include "engine/models/texture_png.h"
#include "lib/png.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace example_mod {

namespace {

constexpr int kPages = 8;   // zone 1's ground pages in textures.res, zone1000.mmp .. zone1007.mmp (512 x 512 DXT1)

// The recolour, a table per channel: red from the green (scaled up), green from the red, blue from the blue (both
// darker). Grass (green over red and blue) comes out red, as the scenario's hue count counts red (twice green and
// blue). Rounded half to even (nearbyint), as the tables were first made.
struct Recolour {
    std::array<std::uint8_t, 256> redFromGreen{}, greenFromRed{}, blueFromBlue{};

    Recolour() {
        for (int v = 0; v < 256; ++v) {
            redFromGreen[v] = static_cast<std::uint8_t>(std::fmin(255.0, std::nearbyint(v * 1.15)));
            greenFromRed[v] = static_cast<std::uint8_t>(std::nearbyint(v * 0.45));
            blueFromBlue[v] = static_cast<std::uint8_t>(std::nearbyint(v * 0.45));
        }
    }
};

std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path PathOf(std::string_view utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::string PageName(int page) { return "zone100" + std::to_string(page); }

// folder\names..., each name matched without case (an install copied to Linux keeps Windows' spellings); the path as
// named where no entry matches.
fs::path Find(const fs::path& folder, std::initializer_list<std::string_view> names) {
    fs::path path = folder;
    for (std::string_view name : names) {
        fs::path exact = path / PathOf(name);
        std::error_code error;
        if (!fs::exists(exact, error) && fs::is_directory(path, error)) {
            for (const fs::directory_entry& entry : fs::directory_iterator(path, error)) {
                if (text::CompareIgnoringCase(Utf8(entry.path().filename()), name) == 0) {
                    exact = entry.path();
                    break;
                }
            }
        }
        path = exact;
    }
    return path;
}

std::optional<std::string> Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string MustRead(const fs::path& path) {
    std::optional<std::string> bytes = Read(path);
    if (!bytes) throw std::runtime_error("no " + Utf8(path));
    return *bytes;
}

void WriteFile(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("could not write " + Utf8(path));
}

// The text with its CR LF line ends as LF.
std::string Lines(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) out.push_back(text[i]);
    return out;
}

std::string WithoutTrailingNewlines(std::string text) {
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

// ---- The ground pages ---------------------------------------------------------------------------------------------

// The install's textures.res entries of the pages: from the archive, or from its folder in an unpacked copy.
std::vector<std::string> PageEntries(const fs::path& install) {
    std::vector<std::string> entries;
    const fs::path archivePath = Find(install, {"Res", "textures.res"});
    if (!fs::exists(archivePath)) {
        const fs::path folder = Find(install, {"Res", "textures"});
        for (int page = 0; page < kPages; ++page) entries.push_back(MustRead(Find(folder, {PageName(page) + ".mmp"})));
        return entries;
    }
    MappedResource archive;
    if (!archive.OpenResource(Utf8(archivePath))) throw std::runtime_error(Utf8(archivePath) + " is not an archive");
    for (int page = 0; page < kPages; ++page) {
        std::uint32_t size = 0;
        void* data = archive.GetFile(PageName(page) + ".mmp", &size);
        if (!data) throw std::runtime_error(Utf8(archivePath) + " has no " + PageName(page) + ".mmp");
        entries.emplace_back(static_cast<const char*>(data), size);
        archive.CloseFile(data);
    }
    return entries;
}

// The PNG ei-convert writes of an .mmp entry.
std::string EntryPng(const std::string& mmp) {
    if (mmp.size() < sizeof(MmpHeader)) throw std::runtime_error("a ground page is shorter than its header");
    MmpHeader header;
    std::memcpy(&header, mmp.data(), sizeof(MmpHeader));
    const texture_png::MmpView view{&header, reinterpret_cast<const std::uint8_t*>(mmp.data()) + sizeof(MmpHeader),
                                    mmp.size() - sizeof(MmpHeader)};
    std::string error;
    std::string png = texture_png::ToPng(view, &error);
    if (png.empty()) throw std::runtime_error("a ground page has no PNG: " + error);
    return png;
}

// Zone 1's ground pages as ei-convert's PNGs: a converted install's own, else made from the install's entries.
std::vector<std::string> GroundPages(const fs::path& install) {
    std::vector<std::string> pages;
    for (int page = 0; page < kPages; ++page) {
        std::optional<std::string> png = Read(Find(install, {"Res", "textures", PageName(page) + ".png"}));
        if (!png) {
            pages.clear();
            break;
        }
        pages.push_back(std::move(*png));
    }
    if (!pages.empty()) return pages;
    for (const std::string& mmp : PageEntries(install)) pages.push_back(EntryPng(mmp));
    return pages;
}

// The "tEXt" chunks of a PNG, whole (length, type, data, CRC): the "ei-texture" settings among them.
std::string TextChunks(std::string_view png) {
    std::string chunks;
    for (std::size_t at = 8; at + 12 <= png.size();) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(png.data() + at);
        const std::size_t length = static_cast<std::size_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
        if (length > png.size() - at - 12) break;
        if (png.substr(at + 4, 4) == "tEXt") chunks.append(png.substr(at, 12 + length));
        at += 12 + length;
    }
    return chunks;
}

// The PNG recoloured (alpha kept; RGB when every texel is opaque, as ei-convert writes it), its text chunks kept after
// the header chunk.
std::string Recoloured(std::string_view png) {
    static const Recolour recolour;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error;
    if (!png::ReadRgba(png, &width, &height, &rgba, &error)) throw std::runtime_error("a ground page: " + error);
    bool opaque = true;
    std::vector<std::uint8_t> samples;
    samples.reserve(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        samples.push_back(recolour.redFromGreen[rgba[i + 1]]);
        samples.push_back(recolour.greenFromRed[rgba[i]]);
        samples.push_back(recolour.blueFromBlue[rgba[i + 2]]);
        samples.push_back(rgba[i + 3]);
        opaque = opaque && rgba[i + 3] == 255;
    }
    if (opaque) {
        std::size_t to = 0;
        for (std::size_t i = 0; i < samples.size(); i += 4)
            for (int c = 0; c < 3; ++c) samples[to++] = samples[i + c];
        samples.resize(to);
    }
    std::string out = png::Write(width, height, opaque ? 3 : 4, samples);
    if (out.size() < 33) throw std::runtime_error("a ground page's PNG was not written");
    out.insert(33, TextChunks(png));   // after the header chunk (IHDR, 25 bytes after the signature)
    return out;
}

// ---- Zone 1's objects and script ----------------------------------------------------------------------------------

// What ei-convert writes for zone 1's .mob: its objects (zone1.mob.toml) and its script (zone1.lua).
struct Zone1 {
    std::string objects;
    std::string script;
};

// A converted install's own files, else made from a copy of the install's zone1.mob alone in `scratch` (the script
// converted as a zone of its own, as `ei-convert <that folder> <target>` writes it).
Zone1 Zone1Converted(const fs::path& install, const fs::path& scratch) {
    std::optional<std::string> objects = Read(Find(install, {"Maps", "zone1.mob.toml"}));
    std::optional<std::string> script = Read(Find(install, {"Maps", "zone1.lua"}));
    if (!objects || !script) {
        const fs::path mob = scratch / "in" / "Maps" / "zone1.mob";
        WriteFile(mob, MustRead(Find(install, {"Maps", "zone1.mob"})));
        if (!objects) {
            convert_mobs::Converted converted = convert_mobs::RoundTrip(mob);
            if (!converted.differences.empty())
                throw std::runtime_error("zone1.mob's objects do not convert: " + converted.differences.front());
            objects = std::move(converted.toml);
        }
        if (!script) {
            const convert_scripts::MissionCounts counts = convert_scripts::WriteMissions(scratch / "in", scratch / "out");
            if (counts.files != 1 || counts.failed != 0) throw std::runtime_error("zone1.mob's script does not convert");
            script = MustRead(scratch / "out" / "Maps" / "zone1.lua");
        }
    }
    return {Lines(std::move(*objects)), Lines(std::move(*script))};
}

// Zone 1's script with `additions` before its WorldScript and a call of ExampleModStart as the WorldScript's first
// line.
std::string Zone1Script(const std::string& script, const std::string& additions) {
    constexpr std::string_view kStart = "function WorldScript()\n";
    std::size_t at = 0;
    while ((at = script.find(kStart, at)) != std::string::npos && at != 0 && script[at - 1] != '\n') ++at;
    if (at == std::string::npos) throw std::runtime_error("zone1.lua has no WorldScript function");
    return script.substr(0, at) + WithoutTrailingNewlines(additions) + "\n\n" + std::string(kStart) +
           "    ExampleModStart()\n" + script.substr(at + kStart.size());
}

void Make(const fs::path& install, const fs::path& mods) {
    const fs::path examples = fs::path(PathOf(platform::ProgramPath())).parent_path() / "examples";
    const fs::path own = examples / "example_mod";
    const fs::path additions = examples / "example_mod_additions" / "Maps";
    if (!fs::is_directory(own)) throw std::runtime_error("no " + Utf8(own) + " (the examples folder beside ei-convert)");
    if (!fs::is_directory(install)) throw std::runtime_error("no game folder " + Utf8(install));
    const fs::path root = mods / "example_mod";
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(own)) {
        if (!entry.is_regular_file()) continue;
        WriteFile(root / fs::relative(entry.path(), own), MustRead(entry.path()));
    }
    const std::vector<std::string> pages = GroundPages(install);
    for (int page = 0; page < kPages; ++page)
        WriteFile(root / "Res" / "textures" / ("Zone100" + std::to_string(page) + ".png"), Recoloured(pages[page]));
    const fs::path scratch = mods / "example_mod.scratch";
    fs::remove_all(scratch);
    const Zone1 zone1 = Zone1Converted(install, scratch);
    fs::remove_all(scratch);
    WriteFile(root / "Maps" / "zone1.mob.toml",
              WithoutTrailingNewlines(zone1.objects) + "\n\n" + Lines(MustRead(additions / "zone1.mob.toml")));
    WriteFile(root / "Maps" / "zone1.lua", Zone1Script(zone1.script, Lines(MustRead(additions / "zone1.lua"))));
    std::printf("ei-convert: %s: the example mod\n", Utf8(root).c_str());
}

}   // namespace

int Write(const fs::path& install, const fs::path& mods) {
    try {
        Make(install, mods);
    } catch (const std::exception& error) {
        std::printf("ei-convert --example-mod: %s\n", error.what());
        return 1;
    }
    return 0;
}

}   // namespace example_mod
