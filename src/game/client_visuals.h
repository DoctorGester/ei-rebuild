#pragma once

// The client's picture kept beside a save: saves\<slot>\visuals.sav, written with the slot's scenario.sav and read back
// by its load (GameFlow::SaveGame / LoadGame). It holds what the picture shows that the game's own save files do not
// keep (docs/notes/renderer.md, "What a save keeps of the picture"); those files stay as the original writes them, and
// a save without the side file starts from the defaults. The file is SaveFileWrite's frame (magic kMagicVisuals)
// around a list of sections, each its kind, its version and its size: a load takes the sections it knows at the
// version it knows and skips the rest, so a section whose contents change takes a new version and an older file falls
// back to the defaults for that part.

#include "random.h"

#include "render/kept_weather.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class ByteStream;
class MapObject;
class WorldClient;

// The sections of visuals.sav (a u32 each, then its version and its size in bytes); End closes the list.
enum class VisualSection : std::uint32_t { End = 0, Weather = 1, Waves = 2, Units = 3, Prints = 4, Generator = 5 };

// A unit's own wetness and snow, by its object id.
struct KeptUnitWeather {
    std::int32_t id = 0;
    render::UnitWeatherLevels levels;
};

class ClientVisuals {
public:
    // The picture's state as the client world and the renderer have it now.
    static ClientVisuals Take(const WorldClient& world);
    // The side file at `path`: written; read (empty when there is none or its frame is broken).
    void Write(const std::string& path) const;
    static ClientVisuals Read(const std::string& path);

    // The sections in the order Save writes them; Load reads the sections it knows and skips the others.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;

    // After the loaded zone's landscape (WorldClient::LoadZone, which emptied the print lists): the surfaces, the
    // clouds and the waves start from the kept state, the ground prints are made again and the client generator goes
    // on from the kept one; the units wait for the next frame (their objects come with the zone's first update).
    // Everything kept is used once.
    void Restore(WorldClient& world);
    // The next frame's light hand-over: each kept unit present among `objects` takes its kept state.
    void RestoreUnits(const std::vector<MapObject*>& objects);

    std::optional<render::KeptWeather> weather;
    std::optional<float> wavePhase;          // the landscape's wave phase (terrain::Terrain::wavePhase)
    std::vector<KeptUnitWeather> units;      // by id, ascending
    std::vector<KeptUnitWeather> dueUnits;   // Restore's units, for RestoreUnits
    // The Prints section's bytes: the client world's blood prints, footprints and scorch marks, each list a u32
    // count and its prints' Save. Kept as written: a print looks its texture up as it is made, so the prints are made
    // again only at the restore, when the zone's textures are loaded.
    std::vector<std::uint8_t> prints;
    std::optional<IsaacRandom> generator;    // g_clientRandom's whole state

private:
    // The prints made again from `prints` into the client world's lists (all three, or none when a list is broken).
    void RestorePrints(WorldClient& world) const;
};
