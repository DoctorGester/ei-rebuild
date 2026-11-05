// The client's picture kept beside a save (client_visuals.h; docs/notes/renderer.md, "What a save keeps of the
// picture").
#include "client_visuals.h"

#include "core_streams.h"
#include "ground_print.h"
#include "random.h"
#include "save_file.h"
#include "save_versions.h"
#include "terrain.h"
#include "texture.h"
#include "world.h"

#include "render/render.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Each section's version: a change of a section's contents takes the next one, and a file of an older version falls
// back to the defaults for that part.
constexpr std::uint32_t kWeatherVersion = 1;
constexpr std::uint32_t kWavesVersion = 1;
constexpr std::uint32_t kUnitsVersion = 1;
constexpr std::uint32_t kPrintsVersion = 1;
constexpr std::uint32_t kGeneratorVersion = 1;
// A section's head: its kind, its version and its size, a u32 each.
constexpr std::uint32_t kSectionHead = 12;
// A kept unit in the Units section: its id, wetness and snow.
constexpr std::uint32_t kUnitBytes = 12;
// The Generator section: seeded, count, results, memory, aa, bb, cc (a u32 each).
constexpr std::uint32_t kGeneratorResults = 256;
constexpr std::uint32_t kGeneratorBytes = 4 * (2 + 2 * kGeneratorResults + 3);

bool Finite(float value) { return std::isfinite(value); }
bool Finite(double value) { return std::isfinite(value); }

void WriteSection(ByteStream* stream, VisualSection kind, std::uint32_t version, MemoryStream& body) {
    stream->WriteValue(static_cast<std::uint32_t>(kind));
    stream->WriteValue(version);
    stream->WriteValue(body.Size());
    stream->Write(body.data.data(), body.Size());
}

// The Weather section: the surfaces' levels, then the clouds' drift and cover. None when a value is not a number.
std::optional<render::KeptWeather> LoadWeather(MemoryStream& body) {
    render::KeptWeather w;
    body.ReadValue(w.wet);
    body.ReadValue(w.puddles);
    body.ReadValue(w.snow);
    for (double& drift : w.cloudDriftA) body.ReadValue(drift);
    for (double& drift : w.cloudDriftB) body.ReadValue(drift);
    body.ReadValue(w.cloudCover);
    const bool finite = Finite(w.wet) && Finite(w.puddles) && Finite(w.snow) && Finite(w.cloudCover) &&
                        std::ranges::all_of(w.cloudDriftA, [](double d) { return Finite(d); }) &&
                        std::ranges::all_of(w.cloudDriftB, [](double d) { return Finite(d); });
    if (!finite || body.Position() != body.Size()) return std::nullopt;
    return w;
}

void SaveWeather(const render::KeptWeather& w, ByteStream* body) {
    body->WriteValue(w.wet);
    body->WriteValue(w.puddles);
    body->WriteValue(w.snow);
    for (double drift : w.cloudDriftA) body->WriteValue(drift);
    for (double drift : w.cloudDriftB) body->WriteValue(drift);
    body->WriteValue(w.cloudCover);
}

// The Units section: the count, then each unit's id, wetness and snow (ascending ids). Empty when the section is malformed.
std::vector<KeptUnitWeather> LoadUnits(MemoryStream& body) {
    std::vector<KeptUnitWeather> units;
    const auto count = body.ReadAs<std::uint32_t>();
    if (static_cast<std::uint64_t>(count) * kUnitBytes != body.Size() - body.Position()) return units;
    units.resize(count);
    for (KeptUnitWeather& unit : units) {
        body.ReadValue(unit.id);
        body.ReadValue(unit.levels.wet);
        body.ReadValue(unit.levels.snow);
    }
    const bool good = std::ranges::is_sorted(units, {}, &KeptUnitWeather::id) &&
                      std::ranges::all_of(units, [](const KeptUnitWeather& unit) {
                          return Finite(unit.levels.wet) && Finite(unit.levels.snow);
                      });
    if (!good) units.clear();
    return units;
}

void SaveUnits(const std::vector<KeptUnitWeather>& units, ByteStream* body) {
    body->WriteValue(static_cast<std::uint32_t>(units.size()));
    for (const KeptUnitWeather& unit : units) {
        body->WriteValue(unit.id);
        body->WriteValue(unit.levels.wet);
        body->WriteValue(unit.levels.snow);
    }
}

// A print list of the Prints section: its count, then each print's Save.
template <typename Print>
void SavePrints(const std::vector<Print>& list, ByteStream* body) {
    body->WriteValue(static_cast<std::uint32_t>(list.size()));
    for (const Print& print : list) print.Save(body);
}

// A print read back makes sense: its place, sizes and angles numbers, its life and lifetime above 0 (Tick divides by
// the lifetime).
bool Sound(const GroundPrint& print) {
    return Finite(print.position.x) && Finite(print.position.y) && Finite(print.halfWidth) &&
           Finite(print.halfLength) && Finite(print.growth) && Finite(print.angle) && Finite(print.uvAngle) &&
           Finite(print.alphaScale) && print.life > 0 && print.lifetime > 0;
}

// A print list read back into `list` (empty). False when the count is more than the bytes left, a read runs past the
// end or a print is not sound.
template <typename Print>
bool LoadPrints(MemoryStream& body, std::vector<Print>* list) {
    const auto count = body.ReadAs<std::uint32_t>();
    if (count > body.Size() - body.Position()) return false;
    list->reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        list->emplace_back(&body);
        if (!body.ok || !Sound(list->back())) return false;
    }
    return body.ok;
}

// The Generator section's state: none when its size is wrong, it was never seeded or its count is past its results.
std::optional<IsaacRandom> LoadGenerator(MemoryStream& body) {
    if (body.Size() != kGeneratorBytes) return std::nullopt;
    IsaacRandom random;
    body.ReadValue(random.seeded);
    body.ReadValue(random.count);
    for (std::uint32_t& word : random.results) body.ReadValue(word);
    for (std::uint32_t& word : random.memory) body.ReadValue(word);
    body.ReadValue(random.aa);
    body.ReadValue(random.bb);
    body.ReadValue(random.cc);
    if (random.seeded == 0 || random.count > kGeneratorResults) return std::nullopt;
    return random;
}

void SaveGenerator(const IsaacRandom& random, ByteStream* body) {
    body->WriteValue(random.seeded);
    body->WriteValue(random.count);
    for (std::uint32_t word : random.results) body->WriteValue(word);
    for (std::uint32_t word : random.memory) body->WriteValue(word);
    body->WriteValue(random.aa);
    body->WriteValue(random.bb);
    body->WriteValue(random.cc);
}

}   // namespace

ClientVisuals ClientVisuals::Take(const WorldClient& world) {
    ClientVisuals kept;
    kept.weather = render::WeatherToKeep();
    if (world.terrain) kept.wavePhase = world.terrain->wavePhase;
    for (const MapObject* object : world.objects) {
        if (!object) continue;
        if (const std::optional<render::UnitWeatherLevels> levels = render::UnitWeatherToKeep(object))
            kept.units.push_back(KeptUnitWeather{object->id, *levels});
    }
    std::ranges::sort(kept.units, {}, &KeptUnitWeather::id);
    MemoryStream body;
    SavePrints(world.bloodPrints, &body);
    SavePrints(world.footprints, &body);
    SavePrints(world.scorchMarks, &body);
    kept.prints.assign(body.data.data(), body.data.data() + body.Size());
    if (g_clientRandom.seeded) kept.generator = g_clientRandom;
    return kept;
}

void ClientVisuals::Write(const std::string& path) const {
    FileStream file;
    if (!file.Open(path, platform::FileMode::Create)) return;
    MemoryStream data;
    data.version = kSaveVersion;
    Save(&data);
    SaveFileWrite(&file, kMagicVisuals, &data);
    file.Close();
}

ClientVisuals ClientVisuals::Read(const std::string& path) {
    ClientVisuals kept;
    FileStream file;
    if (!file.Open(path, platform::FileMode::Read)) return kept;
    MemoryStream data;
    if (SaveFileRead(&data, kMagicVisuals, &file)) kept.Load(&data);
    file.Close();
    return kept;
}

void ClientVisuals::Load(ByteStream* stream) {
    while (stream->Size() - stream->Position() >= kSectionHead) {
        const auto kind = static_cast<VisualSection>(stream->ReadAs<std::uint32_t>());
        const auto version = stream->ReadAs<std::uint32_t>();
        const auto size = stream->ReadAs<std::uint32_t>();
        if (kind == VisualSection::End || size > stream->Size() - stream->Position()) break;
        std::vector<std::uint8_t> bytes(size);
        stream->Read(bytes.data(), size);
        MemoryStream body;
        body.Fill(bytes.data(), size);
        switch (kind) {
        case VisualSection::Weather:
            if (version == kWeatherVersion) weather = LoadWeather(body);
            break;
        case VisualSection::Waves:
            if (version == kWavesVersion && size == sizeof(float)) {
                const auto phase = body.ReadAs<float>();
                if (Finite(phase)) wavePhase = phase;
            }
            break;
        case VisualSection::Units:
            if (version == kUnitsVersion) units = LoadUnits(body);
            break;
        case VisualSection::Prints:
            if (version == kPrintsVersion) prints = std::move(bytes);
            break;
        case VisualSection::Generator:
            if (version == kGeneratorVersion) generator = LoadGenerator(body);
            break;
        default:
            break;
        }
    }
}

void ClientVisuals::Save(ByteStream* stream) const {
    MemoryStream body;
    if (weather) {
        SaveWeather(*weather, &body);
        WriteSection(stream, VisualSection::Weather, kWeatherVersion, body);
    }
    if (wavePhase) {
        body.Resize(0);
        body.WriteValue(*wavePhase);
        WriteSection(stream, VisualSection::Waves, kWavesVersion, body);
    }
    body.Resize(0);
    SaveUnits(units, &body);
    WriteSection(stream, VisualSection::Units, kUnitsVersion, body);
    body.Resize(0);
    body.Write(prints.data(), static_cast<std::uint32_t>(prints.size()));
    WriteSection(stream, VisualSection::Prints, kPrintsVersion, body);
    if (generator) {
        body.Resize(0);
        SaveGenerator(*generator, &body);
        WriteSection(stream, VisualSection::Generator, kGeneratorVersion, body);
    }
    stream->WriteValue(static_cast<std::uint32_t>(VisualSection::End));
    stream->WriteValue(std::uint32_t{0});
    stream->WriteValue(std::uint32_t{0});
}

void ClientVisuals::Restore(WorldClient& world) {
    if (weather) render::RestoreWeather(*weather);
    if (wavePhase && world.terrain) world.terrain->wavePhase = *wavePhase;
    if (!prints.empty()) RestorePrints(world);
    if (generator) g_clientRandom = *generator;
    weather.reset();
    wavePhase.reset();
    prints.clear();
    generator.reset();
    dueUnits = std::exchange(units, {});
}

void ClientVisuals::RestorePrints(WorldClient& world) const {
    MemoryStream body;
    body.Fill(prints.data(), static_cast<std::uint32_t>(prints.size()));
    std::vector<BloodPrint> blood;
    std::vector<Footprint> feet;
    std::vector<GroundPrint> scorch;
    if (LoadPrints(body, &blood) && LoadPrints(body, &feet) && LoadPrints(body, &scorch) &&
        body.Position() == body.Size()) {
        world.bloodPrints = std::move(blood);
        world.footprints = std::move(feet);
        world.scorchMarks = std::move(scorch);
    }
}

void ClientVisuals::RestoreUnits(const std::vector<MapObject*>& objects) {
    if (dueUnits.empty()) return;
    for (const MapObject* object : objects) {
        if (!object) continue;
        const auto it = std::ranges::lower_bound(dueUnits, object->id, {}, &KeptUnitWeather::id);
        if (it != dueUnits.end() && it->id == object->id) render::RestoreUnitWeather(object, it->levels);
    }
    dueUnits.clear();
}
