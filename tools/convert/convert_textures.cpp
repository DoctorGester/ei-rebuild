// ei-convert's textures: convert_textures.h. The tolerances and why: docs/notes/mods.md, "Textures as PNG".
#include "convert_textures.h"

#include "engine/models/texture_png.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace convert_textures {

namespace {

// The mean difference of a channel, at most: over the first level of a DXT format (its blocks made again: exact where
// a block's two colours are among its texels, else stb_dxt's), and over all the levels below it of any format (ours
// from the box filter against those the file has, made by another filter from the original picture): the colour of
// the texels opaque in both (alpha 128 or more), and the alpha of all of them. The levels below are only bounded, not
// matched: the shipped ones' filter is not ours (docs/notes/mods.md has the worst files' means); levels read from the
// wrong bytes differ by 50 and more. The alpha is not compared below a first level with no transparent texel: some
// shipped files have transparent texels in their levels below (a DXT1 block's third mode by mistake), ours none.
constexpr double kDxtFirstLevelMean = 0.5;
constexpr double kLevelsBelowMean = 32.0;

// What the round trips of one format measured.
struct Measures {
    int files = 0;
    long long texels = 0;               // first levels'
    long long clampedTexels = 0;        // premultiplied texels whose colour was above their alpha
    int clampedFiles = 0;
    long long firstDiffering = 0;       // first-level texels not the same (after the clamp)
    int firstMost = 0;                  // the largest channel difference at the first level
    double firstWorstMean = 0;          // the largest mean channel difference of a file's first level
    long long belowTexels = 0;          // the levels below the first
    long long belowDiffering = 0;
    int belowMost = 0;
    double belowWorstColour = 0;        // the largest mean colour difference over a file's levels below
    double belowWorstAlpha = 0;         // the same of the alpha
    int shippedHoles = 0;               // files whose shipped levels below are transparent under an opaque first level
    double loadSeconds = 0;             // FromPng's time
};
std::map<std::string, Measures> g_measures;

struct Difference {
    long long differing = 0;
    int most = 0;
    double mean = 0;
};

// The two levels' channels compared (alpha too).
Difference Compare(const texture_png::Image& a, const texture_png::Image& b) {
    Difference d;
    long long sum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        bool same = true;
        for (int c = 0; c < 4; ++c) {
            const int off = std::abs(a.rgba[i + c] - b.rgba[i + c]);
            same = same && off == 0;
            d.most = std::max(d.most, off);
            sum += off;
        }
        d.differing += same ? 0 : 1;
    }
    d.mean = a.rgba.empty() ? 0.0 : static_cast<double>(sum) / static_cast<double>(a.rgba.size());
    return d;
}

// The levels below the first: the colour's difference over the texels opaque in both, the alpha's over all.
struct BelowDifference {
    double colourSum = 0;
    long long colourTexels = 0;
    double alphaSum = 0;
    long long texels = 0;
    bool holes = false;   // the original has texels of alpha under 128

    void Add(const texture_png::Image& a, const texture_png::Image& b) {
        for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
            ++texels;
            alphaSum += std::abs(a.rgba[i + 3] - b.rgba[i + 3]);
            holes = holes || a.rgba[i + 3] < 128;
            if (a.rgba[i + 3] < 128 || b.rgba[i + 3] < 128) continue;
            ++colourTexels;
            for (int c = 0; c < 3; ++c) colourSum += std::abs(a.rgba[i + c] - b.rgba[i + c]) / 3.0;
        }
    }
    double Colour() const { return colourTexels ? colourSum / static_cast<double>(colourTexels) : 0.0; }
    double Alpha() const { return texels ? alphaSum / static_cast<double>(texels) : 0.0; }
};

bool IsDxt(MmpFormat format) { return texture_png::FormatName(format).starts_with("DXT"); }

bool Premultiplied(MmpFormat format) {
    const std::string_view name = texture_png::FormatName(format);
    return name == "8888" || name == "PNT3" || name == "DXT2" || name == "DXT4";
}

std::string Line(const char* format, double a, double b) {
    char text[200];
    std::snprintf(text, sizeof(text), format, a, b);
    return text;
}

} // namespace

std::string CountName(const std::string& mmp) {
    if (mmp.size() < sizeof(MmpHeader)) return "texture (short)";
    MmpHeader h;
    std::memcpy(&h, mmp.data(), sizeof(MmpHeader));
    const std::string_view name = texture_png::FormatName(h.format);
    return "texture " + (name.empty() ? std::string("of another format") : std::string(name));
}

std::vector<std::string> RoundTrip(const std::string& mmp, std::string* png) {
    if (mmp.size() < sizeof(MmpHeader)) return {"shorter than its header"};
    MmpHeader h;
    std::memcpy(&h, mmp.data(), sizeof(MmpHeader));
    const texture_png::MmpView original{&h, reinterpret_cast<const std::uint8_t*>(mmp.data()) + sizeof(MmpHeader),
                                        mmp.size() - sizeof(MmpHeader)};
    std::string error;
    *png = texture_png::ToPng(original, &error);
    if (png->empty()) return {"no PNG: " + error};
    std::vector<std::uint8_t> made;
    const auto start = std::chrono::steady_clock::now();
    const bool madeOk = texture_png::FromPng(*png, nullptr, &made, &error);
    Measures& m = g_measures[std::string(texture_png::FormatName(h.format))];
    m.loadSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!madeOk) return {"the PNG does not make an .mmp: " + error};
    ++m.files;

    std::vector<std::string> lines;
    MmpHeader back;
    std::memcpy(&back, made.data(), sizeof(MmpHeader));
    const texture_png::MmpView again{&back, made.data() + sizeof(MmpHeader), made.size() - sizeof(MmpHeader)};
    // Every header field the same; PNT3's level count is its packed bytes, which are packed anew.
    MmpHeader want = h;
    if (texture_png::FormatName(h.format) == "PNT3") want.mipCount = back.mipCount;
    if (std::memcmp(&want, &back, sizeof(MmpHeader)) != 0) lines.push_back("the header differs");
    if (std::memcmp(original.body, again.body, h.extraSize) != 0) lines.push_back("the extra block differs");

    const unsigned levels = texture_png::FormatName(h.format) == "PNT3" ? 1 : std::min(h.mipCount, 16u);
    BelowDifference below;
    bool firstOpaque = true;
    for (unsigned level = 0; level < levels; ++level) {
        texture_png::Image a, b;
        if (!texture_png::ReadLevel(original, level, &a)) {
            if (level == 0) lines.push_back("its first level does not decode");
            break;
        }
        if (!texture_png::ReadLevel(again, level, &b)) {
            lines.push_back("level " + std::to_string(level) + " of the PNG's does not decode");
            break;
        }
        if (level == 0 && Premultiplied(h.format)) {
            // A straight PNG holds a premultiplied texel's colour up to its alpha: above, it comes back as the alpha.
            bool clamped = false;
            for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
                bool over = false;
                for (int c = 0; c < 3; ++c)
                    if (a.rgba[i + c] > a.rgba[i + 3]) {
                        a.rgba[i + c] = a.rgba[i + 3];
                        over = true;
                    }
                m.clampedTexels += over ? 1 : 0;
                clamped = clamped || over;
            }
            m.clampedFiles += clamped ? 1 : 0;
        }
        const Difference d = Compare(a, b);
        const long long texels = static_cast<long long>(a.rgba.size() / 4);
        if (level == 0) {
            m.texels += texels;
            m.firstDiffering += d.differing;
            m.firstMost = std::max(m.firstMost, d.most);
            m.firstWorstMean = std::max(m.firstWorstMean, d.mean);
            for (std::size_t i = 3; i < a.rgba.size(); i += 4) firstOpaque = firstOpaque && a.rgba[i] == 255;
            if (!IsDxt(h.format) && d.differing) lines.push_back("the first level differs in " + std::to_string(d.differing) + " texels");
            if (IsDxt(h.format) && d.mean > kDxtFirstLevelMean)
                lines.push_back(Line("the first level's channels differ by %.2f on average (at most %.0f)", d.mean, kDxtFirstLevelMean));
        } else {
            m.belowTexels += texels;
            m.belowDiffering += d.differing;
            m.belowMost = std::max(m.belowMost, d.most);
            below.Add(a, b);
        }
    }
    const bool shippedHoles = firstOpaque && below.holes;
    m.shippedHoles += shippedHoles ? 1 : 0;
    m.belowWorstColour = std::max(m.belowWorstColour, below.Colour());
    if (!shippedHoles) m.belowWorstAlpha = std::max(m.belowWorstAlpha, below.Alpha());
    if (below.Colour() > kLevelsBelowMean)
        lines.push_back(Line("the levels below the first differ in colour by %.2f on average (at most %.0f)", below.Colour(),
                             kLevelsBelowMean));
    if (!shippedHoles && below.Alpha() > kLevelsBelowMean)
        lines.push_back(Line("the levels below the first differ in alpha by %.2f on average (at most %.0f)", below.Alpha(),
                             kLevelsBelowMean));
    return lines;
}

void PrintMeasures() {
    for (const auto& [format, m] : g_measures) {
        std::printf("ei-convert: texture %s: %d files; first levels: %lld texels, %lld differ (%lld clamped in %d files), "
                    "by at most %d, the worst file's mean %.3f; levels below: %lld texels, %lld differ, by at most %d, "
                    "the worst file's mean colour %.3f and alpha %.3f (%d files with shipped holes); made from the PNG "
                    "in %.1f s\n",
                    format.c_str(), m.files, m.texels, m.firstDiffering, m.clampedTexels, m.clampedFiles, m.firstMost,
                    m.firstWorstMean, m.belowTexels, m.belowDiffering, m.belowMost, m.belowWorstColour, m.belowWorstAlpha,
                    m.shippedHoles, m.loadSeconds);
    }
}

} // namespace convert_textures
