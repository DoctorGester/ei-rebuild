#pragma once

// The client's picture kept beside a save: saves\<slot>\visuals.sav, written with the slot's scenario.sav
// (GameFlow::SaveGame), and beside a zone the local client left: saves\current\<zone>.visuals.sav, beside the zone's
// own <zone>.sav (GameFlow::ProcessRequest; each client its own, a multiplayer client in its own saves folder), copied
// with the visited zones' files. A load puts the slot's file in its zone's place (GameFlow::LoadGame), and the zone's
// load reads and removes it (WorldClient::LoadZone). It holds what the picture shows that the game's own save files do
// not keep (docs/notes/renderer.md, "What a save keeps of the picture"); those files stay as the original writes them,
// and a zone without the side file starts from the defaults. The file is SaveFileWrite's frame (magic kMagicVisuals)
// around a list of sections, each its kind, its version and its size: a load takes the sections it knows at the
// version it knows and skips the rest, so a section whose contents change takes a new version and an older file falls
// back to the defaults for that part. One section is the server's, not the picture's: the weather's delay
// (WeatherDelay), which mission.sav does not keep either.

#include "engine/core/random.h"

#include "render/kept_weather.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ByteStream;
class MapObject;
class WorldClient;

// The sections of visuals.sav (a u32 each, then its version and its size in bytes); End closes the list.
enum class VisualSection : std::uint32_t {
    End = 0, Weather = 1, Waves = 2, Units = 3, Prints = 4, Generator = 5, Zone = 6, WeatherDelay = 7,
    ParticleGenerator = 8
};

// A unit's own wetness and snow, by its object id.
struct KeptUnitWeather {
    std::int32_t id = 0;
    render::UnitWeatherLevels levels;
};

// Which zone's picture it is: the client world's tick and the zone's map when it was taken.
struct KeptZone {
    std::int32_t tick = 0;
    std::string map;
};

// The side file of zone `zone` (its map database name): its name, and its path in saves\current.
std::string ZoneVisualsName(std::string_view zone);
std::string ZoneVisualsPath(std::string_view zone);

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

    // After the loaded zone's landscape (WorldClient::LoadZone, which emptied the print lists), when the picture is
    // the zone's (its map, and kept no later than kTickSlack ticks after the zone's tick; a file without the Zone
    // section is taken as it is): the surfaces, the clouds and the waves start from the kept state, the ground prints
    // are made again, aged by the ticks the zone ran since it was kept (a revisit's catch-up, the time a multiplayer
    // client was away), and the client generator and the particles' go on from the kept ones (a file with the client
    // generator but not the particles' seeds the particles' from it, as World::Create does); the units wait for the
    // next frame (their objects come with the zone's first update). Everything kept is used once.
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
    std::optional<IsaacRandom> generator;    // g_clientRandom's whole state (a save's; a zone's side file has none)
    std::optional<IsaacRandom> particleGenerator;   // g_particleRandom's, as `generator`
    std::optional<KeptZone> zone;
    // The server's WorldServer::weatherDelay (a slot's file only: GameFlow::SaveGame sets it, GameFlow::LoadGame
    // gives it back before the zone loads; the delay goes on through the zones of a session, as the generator does).
    std::optional<std::int32_t> weatherDelay;

private:
    // The prints made again from `prints` into the client world's lists (all three, or none when a list is broken),
    // aged by `away` ticks.
    void RestorePrints(WorldClient& world, int away) const;
};
