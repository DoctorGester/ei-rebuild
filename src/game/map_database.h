#pragma once

// Record layouts of the map database (CMapDatabase, object_tables.h) as its loader (0045EA20,
// map_database.cpp) fills them from map.txt / map-LMP.txt. Names are provisional.
//
// The file is read line by line through the text manager (007AF788) and split into words. Lines
// starting with a directive word set the current record:
//   #allod <name>, then 5 lines with one word each (the allod's other names)
//   #zone <name> <allod> <game|brief|edge>       a new map, the current one from here
//   #res, #camera, #maps, #figure (each followed by a line of words), #weather, #cage, #restrict,
//   #sky                                         fields of the current map
//   #exit (then "<target> <n>")                  a new exit of the current map
//   #deploy, #remove, #passtime, #view, #deployangle   fields of the current exit

#include <cstddef>
#include <cstdint>

namespace mapdb {

// AllodRecord (0x2C, constructed by 00460720).
namespace allod {
constexpr std::size_t kSize = 0x2C;
constexpr std::size_t kNames = 0x00;       // 6 CStrings: "#allod" word 2, then one per line
constexpr std::size_t kMaps = 0x18;        // CDWordArray (vtable 0073C3D0): indices of its maps
} // namespace allod

// MapRecord (0x5C, constructed by 004607E0).
namespace map {
constexpr std::size_t kSize = 0x5C;
constexpr std::size_t kName = 0x00;        // CString, "#zone" word 2
constexpr std::size_t kAllod = 0x04;       // CString, "#zone" word 3
constexpr std::size_t kRes2 = 0x08;        // CString, "#res" line word 2
constexpr std::size_t kRes1 = 0x0C;        // CString, "#res" line word 1
constexpr std::size_t kFigure = 0x10;      // CString, "#figure" line
constexpr std::size_t kMaps2 = 0x14;       // CString, "#maps" second line word 2
constexpr std::size_t kMaps1 = 0x18;       // CString, "#maps" second line word 1
constexpr std::size_t kCamera = 0x1C;      // CString, "#camera" line
constexpr std::size_t kSizeX = 0x20;       // int, "#maps" first line
constexpr std::size_t kSizeY = 0x24;       // int
constexpr std::size_t kKind = 0x28;        // 0 game, 2 brief, 1 edge
constexpr std::size_t kWeather = 0x2C;     // 0 none, 1 (default), 2 snow, 3 tornado
constexpr std::size_t kCave = 0x30;        // "#sky cave"
constexpr std::size_t kRestricted = 0x34;  // byte, "#restrict"
constexpr std::size_t kRestrict = 0x38;    // 3 floats
constexpr std::size_t kCage = 0x44;        // byte, "#cage"
constexpr std::size_t kExits = 0x48;       // CArray of exits (vtable at +0x48)
} // namespace map

// Exit (0x3C, constructed by 004606A0).
namespace exit {
constexpr std::size_t kSize = 0x3C;
constexpr std::size_t kPassTime = 0x00;    // float, "#passtime"
constexpr std::size_t kDeploy = 0x04;      // 4 floats, "#deploy" (also copied to kRemove)
constexpr std::size_t kRemove = 0x14;      // 4 floats, "#remove" / "#deploy"
constexpr std::size_t kAngle = 0x24;       // float radians: word 5 of "#deploy" / "#remove"
constexpr std::size_t kTarget = 0x28;      // CString, "#exit" line word 1
constexpr std::size_t kTargetIndex = 0x2C; // int, "#exit" line word 2, minus 1
constexpr std::size_t kView = 0x30;        // float radians, "#view"
constexpr std::size_t kHasDeployAngle = 0x34;   // byte, "#deployangle"
constexpr std::size_t kDeployAngle = 0x38; // float radians
} // namespace exit

} // namespace mapdb
