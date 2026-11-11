// The "graphthreads" step (MF13d1): zones' AI region graphs built on the processors (AIMap::BuildRegionGraph,
// RegionMap::UpdateBlocks) against one local search measuring every block in order, as UpdateBlocks did before.
#include "image_side.h"

#include "../../../src/platform/log.h"

#include "engine/core/core_streams.h"
#include "game/ai/ai_map.h"
#include "game/ai/sector_map.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/map/map_database.h"
#include "game/world/world.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ei::verify::image {

namespace {

std::vector<std::uint8_t> SavedBlocks(RegionMap& regions) {
    MemoryStream stream;
    regions.SaveBlocks(&stream);
    std::vector<std::uint8_t> bytes(stream.Size());
    stream.Seek(0);
    if (!bytes.empty()) stream.Read(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

// BuildRegionGraph's steps with the region map's own local search for every block, row by row.
void BuildWithOneSearch(AIMap& map) {
    std::vector<Unit*> units;
    for (const Sector& sector : map.sectorMap.sectors) units.insert(units.end(), sector.units.begin(), sector.units.end());
    for (Unit* unit : units) map.sectorMap.Remove(unit);
    RegionMap& regions = map.regionMap;
    regions.Reset();
    regions.Attach(&map);
    const int wide = map.cells.width / 8, high = map.cells.height / 8;
    for (int y = 0; y < high; ++y)
        for (int x = 0; x < wide; ++x) regions.PickRepresentatives(&map, &regions.local, {x, y});
    for (int y = 0; y < high; ++y)
        for (int x = 0; x < wide; ++x) regions.MeasureBlockEdges(&regions.local, {x, y});
    regions.ClampEdgeCosts();
    regions.AttachSearches();
    regions.LabelRegions();
    for (Unit* unit : units) map.sectorMap.Add(unit);
}

double MillisecondsSince(std::chrono::steady_clock::time_point began) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
}

} // namespace

std::vector<std::string> GraphThreads(const std::vector<std::string>& zones, std::vector<std::string>* failures) {
    std::vector<std::string> lines;
    for (const std::string& zone : zones) {
        const MapRecord* found = g_mapDatabase.FindMap(zone);
        if (!found || found->map.empty()) {
            lines.push_back("graphthreads: " + zone + " has no map");
            continue;
        }
        const MapRecord record = *found;
        g_serverWorld.TeardownZone();
        if (!g_serverWorld.NewZone(record.map + ".mpr", record.mob + ".mob", record.name)) {
            lines.push_back("graphthreads: " + record.name + " not loaded");
            continue;
        }
        AIMap& map = g_serverWorld.aiMap;
        auto began = std::chrono::steady_clock::now();
        map.BuildRegionGraph();
        const double threaded = MillisecondsSince(began);
        const std::vector<std::uint8_t> spread = SavedBlocks(map.regionMap);
        began = std::chrono::steady_clock::now();
        BuildWithOneSearch(map);
        const double single = MillisecondsSince(began);
        const bool same = SavedBlocks(map.regionMap) == spread;
        char line[300];
        std::snprintf(line, sizeof(line), "graphthreads: %s (%s, %s) %d x %d blocks: %s", record.name.c_str(),
                      record.map.c_str(), record.mob.c_str(), map.cells.width / 8, map.cells.height / 8,
                      same ? "the same" : "DIFFERENT");
        ei::Log("harness: %s; %.0f ms on threads, %.0f ms with one search", line, threaded, single);
        lines.push_back(line);
        if (!same) failures->push_back(line);
    }
    g_gameFlow.StartZone("", ZoneMode::MainMenu);
    return lines;
}

} // namespace ei::verify::image
