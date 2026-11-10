// ei-convert's map objects (convert_mobs.h; docs/notes/mods.md, "Map objects as TOML").
#include "convert_mobs.h"

#include "game/map/map_spec.h"
#include "game/map/mob_toml.h"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace convert_mobs {

namespace {

struct Measures {
    int files = 0;
    std::map<std::string, int> records;   // by kind
    int repeatedIds = 0;                  // records whose id a later record of the file took
    long long mobBytes = 0;
    long long tomlBytes = 0;
    double readSeconds = 0.0;
};
Measures g_measures;

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool SameBits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

bool SameBits(const Vector3& a, const Vector3& b) { return SameBits(a.x, b.x) && SameBits(a.y, b.y) && SameBits(a.z, b.z); }

void Compare(const mapspec::MobContents& a, const mapspec::MobContents& b, std::vector<std::string>& lines) {
    if (a.appliesRelations != b.appliesRelations) lines.push_back("applies_relations");
    if (a.weather.size() != b.weather.size()) lines.push_back("weather count");
    for (std::size_t i = 0; i < a.weather.size() && i < b.weather.size(); ++i) {
        const mapspec::MobWeather& x = a.weather[i];
        const mapspec::MobWeather& y = b.weather[i];
        if (!SameBits(x.wind, y.wind) || !SameBits(x.windStrength, y.windStrength) || !SameBits(x.time, y.time))
            lines.push_back("weather " + std::to_string(i));
    }
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j)
            if (a.diplomacy.relations[i][j] != b.diplomacy.relations[i][j])
                lines.push_back("relation " + std::to_string(i) + " " + std::to_string(j));
    if (a.diplomacy.names != b.diplomacy.names) lines.push_back("player names");
    // The records in the zone loader's order.
    if (a.records.byId.size() != b.records.byId.size()) lines.push_back("record count");
    auto x = a.records.byId.begin();
    auto y = b.records.byId.begin();
    for (; x != a.records.byId.end() && y != b.records.byId.end(); ++x, ++y) {
        const std::string at = "record " + std::to_string(x->first);
        if (x->first != y->first) {
            lines.push_back(at + ": walked where the TOML's has " + std::to_string(y->first));
            continue;
        }
        const auto imagesA = mobtoml::FieldImages(*x->second);
        const auto imagesB = mobtoml::FieldImages(*y->second);
        if (imagesA.size() != imagesB.size()) {
            lines.push_back(at + ": field count");
            continue;
        }
        for (std::size_t k = 0; k < imagesA.size(); ++k)
            if (imagesA[k] != imagesB[k]) lines.push_back(at + ": " + imagesA[k].first);
    }
}

} // namespace

Converted RoundTrip(const std::filesystem::path& path) {
    Converted converted;
    mapspec::MobFile file;
    if (!file.Open(Utf8(path), platform::FileMode::Read)) {
        converted.differences.push_back("the .mob did not open");
        return converted;
    }
    mapspec::MobContents original = file.ReadChunks();
    const long long size = file.file.Size();
    file.file.Close();
    const std::string script = original.scripts.empty() ? std::string() : Utf8(path.stem()) + ".lua";
    converted.toml = mobtoml::Write(original, script);
    const auto start = std::chrono::steady_clock::now();
    const std::optional<mapspec::MobContents> back = mobtoml::Read(converted.toml, Utf8(path) + ".toml");
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!back) {
        converted.differences.push_back("the TOML did not read");
        return converted;
    }
    Compare(original, *back, converted.differences);

    Measures& m = g_measures;
    ++m.files;
    for (const auto& [id, spec] : original.records.byId) ++m.records[std::string(mobtoml::KindName(spec->kind))];
    m.repeatedIds += static_cast<int>(original.records.read.size() - original.records.byId.size());
    m.mobBytes += size;
    m.tomlBytes += static_cast<long long>(converted.toml.size());
    m.readSeconds += seconds;
    return converted;
}

void PrintMeasures() {
    const Measures& m = g_measures;
    if (m.files == 0) return;
    std::string kinds;
    for (const auto& [kind, count] : m.records) kinds += (kinds.empty() ? "" : ", ") + std::to_string(count) + " " + kind;
    std::printf("ei-convert: map objects: %d files, %s; %d records of a repeated id; %.1f MB as .mob, %.1f MB as TOML; "
                "read from the TOML in %.2f s\n",
                m.files, kinds.c_str(), m.repeatedIds, m.mobBytes / 1e6, m.tomlBytes / 1e6, m.readSeconds);
}

}   // namespace convert_mobs
