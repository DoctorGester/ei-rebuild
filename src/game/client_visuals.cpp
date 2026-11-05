// The client's picture kept beside a save (client_visuals.h; docs/notes/renderer.md, "What a save keeps of the
// picture").
#include "client_visuals.h"

#include "core_streams.h"
#include "save_file.h"
#include "save_versions.h"
#include "terrain.h"
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
// A section's head: its kind, its version and its size, a u32 each.
constexpr std::uint32_t kSectionHead = 12;
// A kept unit in the Units section: its id, wetness and snow.
constexpr std::uint32_t kUnitBytes = 12;

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
    stream->WriteValue(static_cast<std::uint32_t>(VisualSection::End));
    stream->WriteValue(std::uint32_t{0});
    stream->WriteValue(std::uint32_t{0});
}

void ClientVisuals::Restore(WorldClient& world) {
    if (weather) render::RestoreWeather(*weather);
    if (wavePhase && world.terrain) world.terrain->wavePhase = *wavePhase;
    weather.reset();
    wavePhase.reset();
    dueUnits = std::exchange(units, {});
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
