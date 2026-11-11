// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses. Plain float maths
// (rule 7). See docs/notes/objects.md, "Zone files and unit records".
#include "game/ai/ai_map.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_transform.h"
#include "game/app/singletons.h"
#include "engine/scene/light.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/map/map_spec.h"
#include "game/ai/sector_map.h"
#include "game/terrain/terrain.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "lib/crt.h"

#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <string>

namespace mapspec {

namespace {

bool ReadFlag(Chunk& c) { return c.ReadByte() == 1; }

// The chunk's file.
void FileRead(Chunk& c, void* data, unsigned size) { c.File()->Read(data, size); }

} // namespace

// ---- The base record ---------------------------------------------------------------

Spec::Spec() : Chunk(nullptr) {
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    std::string none("none");
    position = {0.0f, 0.0f, 0.0f};
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    name = none;
    comment = none;
}

Spec::~Spec() = default;

ObjectSpec::ObjectSpec() {
    std::string none("none");
    kind = SpecKind::Object;
    model.clear();
    texture = none;
    primaryTexture = none;
    scale = {0.5f, 0.5f, 0.5f};
    parentIname = none;
    skinTexture = none;
}

ObjectSpec::~ObjectSpec() = default;

void ObjectSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xB002: id = c.ReadInt(); break;
    case 0xB003: parentId = c.ReadInt(); break;
    case 0xB004: name = c.ReadString(); break;
    case 0xB006: texture = c.ReadString(); break;
    case 0xB007: parentIname = c.ReadString(); break;
    case 0xB008: skinTexture = c.ReadString(); break;
    case 0xB009: position = c.ReadVector3(); break;
    case 0xB00A: rotation = c.ReadQuaternion(); break;
    case 0xB00C:
        scale.y = c.ReadFloat();
        scale.x = c.ReadFloat();
        scale.z = c.ReadFloat();
        break;
    case 0xB00D: parts = c.ReadStringArray(); break;
    case 0xB00E: primaryTexture = c.ReadString(); break;
    case 0xB00F: comment = c.ReadString(); break;
    case 0xB011: side = c.ReadByte(); break;
    case 0xB012: player = c.ReadInt(); break;
    case 0xB013: shadow = ReadFlag(c); break;
    case 0xB014: visible = ReadFlag(c); break;
    case 0xB016: model = c.ReadString(); break;
    default: break;
    }
}

FlameSpec::FlameSpec() {
    offset = {0.0f, 0.0f, 0.0f};
    kind = SpecKind::Flame;
    sound = "nature\\fire\\small.wav";
}

FlameSpec::~FlameSpec() = default;

void FlameSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xBBBF0001: volume = c.ReadFloat(); break;
    case 0xBBBF0002: offset = c.ReadVector3(); break;
    case 0xBBBF0003: sound = c.ReadString(); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

// ---- Magic traps --------------------------------------------------------------------

TrapSpec::TrapSpec() {
    trap.sides = ~0u;
    trap.period = 15;
    kind = SpecKind::Trap;
}

TrapSpec::~TrapSpec() = default;

void TrapSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xBBAB0001: trap.sides = static_cast<std::uint32_t>(c.ReadInt()); break;
    case 0xBBAB0002: trap.spell = c.ReadString(); break;
    case 0xBBAB0003: {
        std::uint32_t n = static_cast<std::uint32_t>(c.ReadInt());
        trap.area.resize(static_cast<int>(n));
        for (std::uint32_t i = 0; i < n; ++i) {
            FileRead(c, &trap.area[i], 12);
            c.Advance(12);
        }
        break;
    }
    case 0xBBAB0004: {
        int n = c.ReadInt();
        trap.points.resize(n);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            FileRead(c, &trap.points[i], 8);
            c.Advance(8);
        }
        break;
    }
    case 0xBBAB0005: trap.period = c.ReadInt(); break;
    case 0xBBAC0005: trap.singleTarget = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

LeverSpec::LeverSpec() {
    lever.lock.mask = 1;
    lever.lock.id = 0;
    lever.lock.level = 0;
    lever.state = 0;
    lever.stateCount = 2;
    lever.directUseBlocked = 0;
    lever.keepsMapFlag = 0;
    lever.cycling = 0;
    kind = SpecKind::Lever;
}

LeverSpec::~LeverSpec() = default;

void LeverSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xBBAC0002: lever.state = c.ReadByte(); break;
    case 0xBBAC0003: lever.stateCount = c.ReadByte(); break;
    case 0xBBAC0004: lever.cycling = ReadFlag(c); break;
    case 0xBBAC0006: c.ReadBytes(&lever.lock, 12); break;
    case 0xBBAC0007: lever.directUseBlocked = ReadFlag(c); break;
    case 0xBBAC0008: lever.keepsMapFlag = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

// ---- Rotating a vector ---------------------------------------------------------------------------

} // namespace mapspec

namespace mapspec {

// ---- The zone's diplomacy table ------------------------------------------------------------------

// The diplomacy of the zone's 32 players (made by the zone loader): every pair 1 but a player
// with itself (0), and the player names "Player-<i>".
ZoneDiplomacy::ZoneDiplomacy() {
    for (auto& row : relations) {
        for (std::int32_t& relation : row) relation = 1;
    }
    for (int i = 0; i < 32; ++i) relations[i][i] = 0;
    for (int i = 0; i < 32; ++i) names.push_back(std::format("Player-{}", i));
}

} // namespace mapspec

// --- Effect ------------------------------------------------------------------------------------------

// The zone file's effect records: particle sources, lights and sounds on their base
// (map_spec.h). They keep the world object they made and its effect, are kept above the terrain when moved,
// and save as chunks DD01 / AA01 / CC01. See docs/notes/objects.md, "Zone files and unit records".

namespace mapspec {

// ---- The base ------------------------------------------------------------------------

// The records' destructors are the base record's.
EffectSpec::~EffectSpec() = default;
ParticleSpec::~ParticleSpec() = default;
LightSpec::~LightSpec() = default;

// ---- Particle sources ------------------------------------------------------------------

ParticleSpec::ParticleSpec() {
    kind = SpecKind::Particle;
}

void ParticleSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xDD02: id = c.ReadInt(); break;
    case 0xDD03: position = c.ReadVector3(); break;
    case 0xDD04: comment = c.ReadString(); break;
    case 0xDD05: name = c.ReadString(); break;
    case 0xDD06: type = c.ReadInt(); break;
    case 0xDD07: scale = c.ReadFloat(); break;
    default: break;
    }
}

LightSpec::LightSpec() {
    colour = {1.0f, 1.0f, 1.0f};
    kind = SpecKind::Light;
}

void LightSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xAA02: radius = c.ReadFloat(); break;
    case 0xAA03: name = c.ReadString(); break;
    case 0xAA04: position = c.ReadVector3(); break;
    case 0xAA05: id = c.ReadInt(); break;
    case 0xAA06: shadows = c.ReadByte() != 0; break;
    case 0xAA07: colour = c.ReadVector3(); break;
    case 0xAA08: comment = c.ReadString(); break;
    default: break;
    }
}

SoundSpec::SoundSpec() {
    kind = SpecKind::Sound;
}

SoundSpec::~SoundSpec() = default;

void SoundSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xCC02: id = c.ReadInt(); break;
    case 0xCC03: position = c.ReadVector3(); break;
    case 0xCC04: volume = c.ReadInt(); break;
    case 0xCC05: name = c.ReadString(); break;
    case 0xCC06: minInterval = c.ReadInt(); break;
    case 0xCC07: maxInterval = c.ReadInt(); break;
    case 0xCC08: comment = c.ReadString(); break;
    case 0xCC0A:
        sounds = c.ReadStringArray();
        break;
    case 0xCC0B: radius = c.ReadInt(); break;
    case 0xCC0D: ambient = c.ReadByte() == 1; break;
    case 0xCC0E: unreadChunk = c.ReadByte() == 1; break;
    default: break;
    }
}

} // namespace mapspec
