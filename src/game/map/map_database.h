#pragma once

// The map database's records: the allods and zones of map.txt (map_database.cpp loads them).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


class ByteStream;

// One "#allod" entry of map.txt.
struct AllodRecord {
    std::string name;           // the "#allod" line's second word
    std::string otherNames[5];  // one word on each of the next five lines
    std::vector<int> maps;    // the indices of its maps
};
// One "#exit" of a map entry (map_database.cpp loads it): the map database's
// record, which the world server copies (SetZoneExits) and sends to the client world.
struct ZoneExit {
    float passTime = 0.0f;          // "#passtime": the time the way takes
    float deploy[4] = {};           // "#deploy": the rectangle the arriving party stands in
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;   // "#remove" (and "#deploy"): the area that leads out
    float angle = 0.0f;             // the area's rotation about its centre (radians)
    std::string target;            // the zone it leads to ("" = none)
    int entrance = 0;               // the target zone's exit the party arrives at
    float view = 0.0f;              // "#view" (radians)
    std::uint8_t hasDeployAngle = 0;   // "#deployangle" given; saved from version 0x68
    float deployAngle = 0.0f;       // saved from version 0x68

    // Whether (x, y) lies inside the exit area (rotated by `angle` about
    // the rectangle's centre when it is not 0).
    bool Contains(float x, float y) const;
    // From / to a stream (core_streams.h): +0x00..+0x20
    // as dwords, the target with its terminator, +0x2C, +0x30, then +0x34 (a byte), +0x38 and the
    // angle; Read takes the last three only from version 0x68 on (else 0).
    void Read(ByteStream* stream);
    void Write(ByteStream* stream) const;
};

// One map/zone entry: +0 name, +4 allod name, +8 and +0xC other names.
struct MapRecord {
    std::string name;
    std::string allod;     // (SaveGame keeps the visited zones of the current one's)
    std::string mob;       // the .mob file without extension
    std::string map;       // the .mpr map without extension ("" = none)
    std::string figure;    // the "#figure" line
    std::string picture2;  // the "#maps" second line's second word
    std::string picture;   // its first word, the HUD map's texture
    std::string camera;    // the "#camera" line
    int sizeX = 0;          // "#maps" first line: the zone's size (times 32: the HUD map's world extent)
    int sizeY = 0;
    int kind = 0;           // 0 game, 1 edge, 2 brief (2: the zone has a script folder)
    int climate = 0;      // WorldServer::SetClimate when the zone starts
    int cave = 0;      // WorldServer::SetCave
    bool restricted = false;   // "#restrict x y radius": the camera stays in that circle
    float restrictX = 0.0f;
    float restrictY = 0.0f;
    float restrictRadius = 0.0f;
    bool cage = false;      // "#cage": a base where the party does not walk to the people
    std::vector<ZoneExit> exits;
};

// The map database loaded from map.txt / map_LMP.txt (a global).
struct MapDatabase {
    // Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(std::string_view name);
    // Allod named `name` (_mbsicmp), or null.
    AllodRecord* FindAllod(std::string_view name);
    // Adds the allods and maps of a map.txt-style text file. Returns false if the file is not found.
    bool Load(const char* fileName);
    // The lists emptied, then "map.txt" and "map-LMP.txt" loaded; returns the second Load's result (WinMain does not
    // read it).
    bool LoadAll();

    std::vector<AllodRecord> allods;
    std::vector<MapRecord> maps;
};
