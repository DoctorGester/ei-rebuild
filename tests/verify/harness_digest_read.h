#pragma once

// What the digest's two readers share (harness_digest.h): the record's entities and fields, and the readers'
// entry points. The reading code (harness_digest_read.inc) is compiled twice: against game.exe's objects through
// their views (harness_digest_exe.cpp, exe_objects.h) for game.exe's runs, and against our classes
// (image/harness_digest_image.cpp) for the image's. Neither this header nor the views include a game header, so our
// classes do not decide what the original's runs record (ticket H30b).

#include "harness_digest.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ei::verify::digest {

// An object's class id as an int: the original's objects return one, the image's a ClassId.
template <typename Object>
int ClassOf(Object* object) { return static_cast<int>(object->GetClassId()); }

// The class ids the digest tells apart (EngineObject::GetClassId).
constexpr int kClassMapObjectServer = 0x39;     // MapObjectServer, a map object's server half
constexpr int kClassUnitServer = 0x3E;          // UnitServer, a unit's controller (UnitController)
constexpr int kClassUnit = 0x50;                // Unit
// The world's effects by class id (world_effect.h): their own state goes into "timed<id>".
constexpr int kClassSound = 0x48, kClassAmbientSound = 0x49, kClassParticle = 0x4A, kClassPointLight = 0x4B,
              kClassLightning = 0x4D, kClassArrow = 0x4E, kClassWall = 0x5A, kClassArrowFigure = 0x5B,
              kClassShowVisibility = 0x5E, kClassSoundSeq = 99, kClassTornado = 0x65;

// A field's value: its name ('#' stands for `index`), how it is written and its 32 bits.
enum class Kind : char { Int, Unsigned, Float, Bits, Mode };
struct Field {
    const char* name;
    int index;
    Kind kind;
    std::uint32_t value;
};

// A name made at run time (party variables), kept for the whole run.
const char* Intern(const std::string& name);

// One entity's fields (world, random, a player, an object) and how a record introduces it.
struct Entity {
    std::string description;
    std::vector<Field> fields;
};

// Entities in record order: the world, the generators, the players by id, the objects by id (digest
// 1); then (digest 2, SV2a) the timed objects' own state by id, the missiles by their place in the
// list, the AI event table, the mission script, the client world and its units by id. A key's name
// starts with its group's (kGroupNames), which tools/golden.py reads: a golden of digest 1 lacks the
// groups after kObject, and its comparisons leave them out. The water layer ("water", R16c3) only after
// the scenario's "digest water" step: the landscape ("water-1") and each cached sector with water
// ("water<sy * 1000 + sx>").
enum Group : int { kWorld = 0, kRandom, kPlayer, kObject, kTimed, kMissile, kEvents, kScript, kClient, kClientUnit, kWater, kGroups };
using Key = std::pair<int, int>;
using State = std::map<Key, Entity>;

// The whole state, and the section hashes (one per group).
struct Snapshot {
    State state;
    std::uint32_t sections[kGroups] = {};
    std::uint32_t all = 0;
    std::string zone;
    int tick = 0;   // the server world's clock
};

// The entity of `key` in `state`, empty (a node of the record before the last reused; harness_digest.cpp).
Entity& Slot(State& state, const Key& key);
// Whether the records hold the water group (WithWater).
bool WaterOn();
// The id of AI mode kind `index` (guard, patrol, sentry, suspection, aggression, corpsewatcher, player, fear, tamed,
// follow): game.exe's vtable of the kind, what the goldens hold.
std::uint32_t ModeKindId(std::size_t index);

struct Builder {
    Entity* entity = nullptr;
    void Int(const char* name, int value, int index = -1) { Add(name, index, Kind::Int, static_cast<std::uint32_t>(value)); }
    void Unsigned(const char* name, std::uint32_t value, int index = -1) { Add(name, index, Kind::Unsigned, value); }
    void Bits(const char* name, std::uint32_t value, int index = -1) { Add(name, index, Kind::Bits, value); }
    void Mode(const char* name, std::uint32_t vtable, int index = -1) { Add(name, index, Kind::Mode, vtable); }
    void Float(const char* name, float value, int index = -1) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        Add(name, index, Kind::Float, bits);
    }
    template <class V>
    void Vector(const char* x, const char* y, const char* z, const V& v) {
        Float(x, v.x);
        Float(y, v.y);
        Float(z, v.z);
    }
    void Add(const char* name, int index, Kind kind, std::uint32_t value) { entity->fields.push_back({name, index, kind, value}); }
};

// FNV-1a over 32-bit words.
struct Hash {
    std::uint32_t h = 2166136261u;
    void Add(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 16777619u;
        }
    }
    void AddText(const char* s) {
        for (; s && *s; ++s) {
            h ^= static_cast<std::uint8_t>(*s);
            h *= 16777619u;
        }
        Add(0);
    }
    void AddText(const std::string& s) { AddText(s.c_str()); }
};

inline std::uint32_t HashWords(const std::uint32_t* words, int count) {
    Hash h;
    for (int i = 0; i < count; ++i) h.Add(words[i]);
    return h.h;
}

inline const char* Text(const char* s) { return s ? s : ""; }

inline std::uint32_t FloatBits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

inline std::uint32_t TextHash(const char* text) {
    Hash h;
    h.AddText(text);
    return h.h;
}
inline std::uint32_t TextHash(const std::string& text) { return TextHash(text.c_str()); }

// The readers (harness_digest_read.inc), each over the server world `server`: the whole state into `s` (Read), a unit
// now (Unit), the local leader (LocalLeader), every world object's id, class and address into the heap trace
// (NoteAddresses). exe_side reads game.exe's objects (harness_digest_exe.cpp), image_side ours
// (image/harness_digest_image.cpp).
namespace exe_side {
void Read(void* server, Snapshot& s);
UnitNow UnitNowOf(void* server, int id);
int LocalLeader(void* server, unsigned* money);
void NoteAddresses(void* server);
} // namespace exe_side
namespace image_side {
void Read(void* server, Snapshot& s);
UnitNow UnitNowOf(void* server, int id);
int LocalLeader(void* server, unsigned* money);
void NoteAddresses(void* server);
} // namespace image_side

} // namespace ei::verify::digest
