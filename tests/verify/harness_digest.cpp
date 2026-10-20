// The game-state digest of harness runs (harness_digest.h).
#include "harness_digest.h"

#include "harness_clock.h"
#include "harness_heap.h"
#include "harness_starter.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "../../src/game/ai_motion.h"
#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/party.h"
#include "../../src/game/random.h"
#include "../../src/game/script.h"
#include "../../src/game/spell.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"
#include "../../src/game/world.h"
#include "../../src/game/world_effect.h"
#include "../../src/lib/crt.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ei::verify::digest {

namespace {

constexpr int kClassUnit = world::kClassUnit;   // 0x50
constexpr int kClassMapObjectServer = 0x39;     // CMapObjectServer, a map object's server half
constexpr int kClassUnitServer = 0x3E;          // CUnitServer, a unit's controller (UnitController)

// The world's effects by class id (world_effect.h): their own state goes into "timed<id>".
constexpr int kClassSound = 0x48, kClassAmbientSound = 0x49, kClassParticle = 0x4A, kClassPointLight = 0x4B,
              kClassLightning = 0x4D, kClassArrow = 0x4E, kClassWall = 0x5A, kClassArrowFigure = 0x5B,
              kClassShowVisibility = 0x5E, kClassSoundSeq = 99, kClassTornado = 0x65;

// The client world (a static object of game.exe) and the mission's script (the world's CScript).
CWorldClient* ClientWorldObject() { return reinterpret_cast<CWorldClient*>(std::uintptr_t(0x007AF5B8)); }
CScript* MissionScriptObject() { return reinterpret_cast<CScript*>(std::uintptr_t(0x007C27D4)); }

// A field's value: its name ('#' stands for `index`), how it is written and its 32 bits.
enum class Kind : char { Int, Unsigned, Float, Bits, Mode };
struct Field {
    const char* name;
    int index;
    Kind kind;
    std::uint32_t value;
};

bool SameName(const Field& a, const Field& b) {
    return a.index == b.index && (a.name == b.name || std::strcmp(a.name, b.name) == 0);
}

// Names made at run time (party variables) live here for the whole run.
const char* Intern(const std::string& name) {
    static std::set<std::string> names;
    return names.insert(name).first->c_str();
}

// One entity's fields (world, random, a player, an object) and how a record introduces it.
struct Entity {
    std::string description;
    std::vector<Field> fields;
};

// Entities in record order: the world, the generators, the players by id, the objects by id (digest
// 1); then (digest 2, SV2a) the timed objects' own state by id, the missiles by their place in the
// list, the AI event table, the mission script, the client world and its units by id. A key's name
// starts with its group's (kGroupNames), which tools/golden.py reads: a golden of digest 1 lacks the
// groups after kObject, and its comparisons leave them out.
enum Group : int { kWorld = 0, kRandom, kPlayer, kObject, kTimed, kMissile, kEvents, kScript, kClient, kClientUnit, kGroups };
constexpr const char* kGroupNames[kGroups] = {"world", "random", "player", "obj", "timed",
                                              "missile", "aievents", "script", "client", "cunit"};
// The groups beyond digest 1's, as the title line lists them.
constexpr const char* kNewGroups = "timed,missile,aievents,script,client,cunit";
using Key = std::pair<int, int>;
using State = std::map<Key, Entity>;

// Groups of one entity are named alone ("world"); the others take the id ("obj12").
bool Single(int group) { return group == kWorld || group == kRandom || group == kEvents || group == kScript || group == kClient; }

std::string KeyName(const Key& key) {
    std::string name = kGroupNames[key.first];
    return Single(key.first) ? name : name + std::to_string(key.second);
}

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
    void Vector(const char* x, const char* y, const char* z, const Vector3& v) {
        Float(x, v.x);
        Float(y, v.y);
        Float(z, v.z);
    }
    void Add(const char* name, int index, Kind kind, std::uint32_t value) { entity->fields.push_back({name, index, kind, value}); }
};

int IdOf(const CEngineObject* object) { return object ? object->id_ : -1; }

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
};

std::uint32_t HashWords(const std::uint32_t* words, int count) {
    Hash h;
    for (int i = 0; i < count; ++i) h.Add(words[i]);
    return h.h;
}

const char* Text(const char* s) { return s ? s : ""; }

// The AI mode a vtable belongs to (unit_ai.h), or null for one not listed.
const char* ModeName(const void* vtable) {
    static const std::pair<const void*, const char*> kModes[] = {
        {aimode::kGuardVtable, "guard"},         {aimode::kPatrolVtable, "patrol"},
        {aimode::kSentryVtable, "sentry"},       {aimode::kSuspectionVtable, "suspection"},
        {aimode::kAggressionVtable, "aggression"}, {aimode::kCorpseWatcherVtable, "corpsewatcher"},
        {aimode::kPlayerVtable, "player"},       {aimode::kFearVtable, "fear"},
        {aimode::kTamedVtable, "tamed"},         {aimode::kFollowVtable, "follow"},
    };
    for (const auto& m : kModes)
        if (m.first == vtable) return m.second;
    return nullptr;
}

// --- Reading the state ---

void ReadWorld(CWorldServer* w, Builder& b) {
    b.Int("tick", w->tick_);
    b.Int("updates", w->updateCount_);
    b.Unsigned("paused", w->paused_);
    b.Float("timeBase", w->timeBase_);
    b.Float("secondsPerTick", w->secondsPerTick_);
    float wind[3];
    std::memcpy(wind, w->windDirection_, sizeof(wind));
    b.Float("wind.x", wind[0]);
    b.Float("wind.y", wind[1]);
    b.Float("wind.z", wind[2]);
    b.Bits("windStrength", w->windStrength_);
    b.Bits("nightLevel", w->nightLevel_);
    b.Bits("weatherFactor", w->weatherFactor_);
    for (int i = 0; i < 5; ++i) {
        const WorldAlarm& a = w->alarms_[i];
        b.Unsigned("alarm#.active", a.active, i);
        b.Int("alarm#.time", a.time, i);
        b.Float("alarm#.x", a.x, i);
        b.Float("alarm#.y", a.y, i);
    }
    b.Int("climate", w->climate_);
    b.Int("cave", w->cave_);
    b.Int("weather", w->weather_);
    b.Float("weatherStrength", w->weatherStrength_);
    b.Int("exits", w->exitCount_);
    b.Int("gameResult", w->gameResult_);
    b.Unsigned("lastRainBoltTick", w->lastRainBoltTick_);
    b.Int("zoneLoaded", w->zoneLoaded_);
    b.Int("scriptsActive", w->scriptsActive_);
    b.Vector("start.x", "start.y", "start.z", w->startPosition_);
    b.Int("occluderReplanTime", w->occluderReplanTime_);
    b.Int("sessionKind", w->sessionKind_);
    b.Unsigned("objects", static_cast<std::uint32_t>(w->objects_.map.Size()));
    b.Unsigned("mapObjects", static_cast<std::uint32_t>(w->mapObjects_.map.Size()));
    b.Unsigned("sentObjects", static_cast<std::uint32_t>(w->sentObjects_.map.Size()));
    b.Unsigned("timedObjects", static_cast<std::uint32_t>(w->timedObjects_.map.Size()));
    b.Unsigned("updateMap", static_cast<std::uint32_t>(w->updateMap_.map.Size()));
    b.Int("projectiles", w->projectiles_.GetCount());
    b.Int("counted", w->counted_.GetCount());
    b.Int("sharedObjects", w->sharedObjects_.GetCount());
    for (int i = 0; i < 32; ++i) {
        b.Bits("allied#", w->diplomacy_.allied_[i], i);
        b.Bits("hostile#", w->diplomacy_.hostile_[i], i);
    }
}

void ReadRandom(Builder& b) {
    const IsaacRandom& r = IsaacRandom::Global();
    b.Unsigned("isaac.seeded", r.seeded_);
    b.Unsigned("isaac.count", r.count_);
    b.Bits("isaac.a", r.a_);
    b.Bits("isaac.b", r.b_);
    b.Bits("isaac.c", r.c_);
    b.Bits("isaac.results", HashWords(r.results_, 256));
    b.Bits("isaac.memory", HashWords(r.memory_, 256));
    // rand()'s state (the CRT's _holdrand, _getptd 006F00D1 +0x14 in game.exe; ours in the
    // image). The game's main thread is the only one that draws.
    b.Bits("crt.rand", libcrt::RandState());
}

void ReadPlayer(WorldPlayer* p, Builder& b) {
    b.Int("id", p->id);
    b.Unsigned("partyId", p->partyId);
    b.Unsigned("sessionKind", p->sessionKind);
    b.Int("lastScreen", p->lastScreen);
    b.Unsigned("godMode", p->godMode);
    b.Int("lastTick", p->lastTick);
    b.Int("items", p->items.GetCount());
    b.Int("ids", p->ids.GetCount());
    b.Unsigned("units", static_cast<std::uint32_t>(p->units.map.Size()));
    b.Unsigned("objects", static_cast<std::uint32_t>(p->objects.map.Size()));
    CParty* party = p->Party();
    b.Unsigned("money", party->money_);
    b.Int("partyItems", party->items_.size);
    b.Int("members", party->members_.GetSize());
    for (int i = 0; i < party->members_.GetSize(); ++i) b.Unsigned("member#.unit", party->members_[i].unitId, i);
    b.Unsigned("entrance", party->entrance_);
    std::vector<std::pair<std::string, float>> variables;
    for (const NamedFloatTable::Node* n = party->variables_.First(); n; n = party->variables_.Next(n))
        variables.emplace_back(Text(n->value.key), n->value.mapped);
    std::sort(variables.begin(), variables.end());
    for (const auto& v : variables) b.Float(Intern("var." + v.first), v.second);
}

void ReadEffects(CMapObject* m, Builder& b) {
    b.Int("effects", m->effectCount);
    for (int i = 0; i < m->effectCount; ++i) {
        const MapEffect& e = m->effects[i];
        b.Unsigned("effect#.type", e.type, i);
        b.Unsigned("effect#.power", e.power, i);
        b.Int("effect#.ticksLeft", e.ticksLeft, i);
        b.Bits("effect#.value", e.value, i);
        b.Bits("effect#.strength", e.strength, i);
    }
}

void ReadBrain(AIBrain& brain, Builder& b) {
    b.Int("ai.action", brain.action);
    b.Vector("ai.point.x", "ai.point.y", "ai.point.z", brain.actionPoint);
    b.Int("ai.looks", brain.looks.GetSize());
    b.Int("ai.actionTime", brain.actionTime);
    b.Int("ai.target", IdOf(brain.target));
    b.Int("ai.seen", IdOf(brain.seen));
    b.Unsigned("ai.alerted", brain.alerted);
    b.Int("ai.alarm", brain.alarm);
    b.Int("ai.alarmSlot", brain.alarmSlot);
    b.Float("ai.callRadius", brain.callRadius);
    b.Int("ai.alertMode", brain.alertMode);
    b.Int("ai.alarmCountdown", brain.alarmCountdown);
    b.Float("ai.near.x", brain.nearX);
    b.Float("ai.near.y", brain.nearY);
    b.Float("ai.decay", brain.decay);
    b.Int("ai.standCooldown", brain.standCooldown);
    b.Int("ai.followTimer", brain.followTimer);
    b.Float("ai.noticed.x", brain.noticedX);
    b.Float("ai.noticed.y", brain.noticedY);
    b.Int("ai.noticedLevel", brain.noticedLevel);
    b.Int("ai.noticedKind", brain.noticedKind);
    b.Int("ai.nearby", brain.nearby.GetCount());
    b.Int("ai.visible", brain.visible.GetCount());
    b.Int("ai.corpses", brain.corpses.GetCount());
    b.Int("ai.reports", brain.reports.GetSize());
    b.Int("ai.calls", brain.calls.GetSize());
    b.Unsigned("ai.seesEnemy", brain.seesEnemy);
    // The modes by their vtables (game.exe's addresses; ModeName lists the known ones).
    auto modeBits = [](const AIMode* mode) { return mode ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(mode->vtable)) : 0u; };
    b.Mode("ai.mode", modeBits(brain.current));
    int i = 0;
    for (auto* n = brain.modes.head; n; n = n->next) b.Mode("ai.modes#", modeBits(n->data), i++);
}

void ReadController(CUnit* u, Builder& b);

void ReadUnit(CUnit* u, const std::map<const void*, int>& players, Builder& b) {
    auto owner = players.find(u->player);
    b.Int("player", u->player ? (owner != players.end() ? owner->second : -2) : -1);
    b.Int("side", u->side);
    b.Bits("relationsA", u->relationsA);
    b.Bits("relationsB", u->relationsB);
    b.Int("dead", u->dead);
    b.Int("animation", u->animationEntry);
    b.Int("animation2", u->secondEntry);
    b.Int("animationStart", u->animationStart);
    b.Int("motionMode", u->motionMode);
    b.Int("moveMode", u->moveMode);
    b.Int("moveMode2", u->moveMode2);
    b.Unsigned("activeWeapon", u->activeWeapon);
    b.Int("pendingWeapon", u->pendingWeapon);
    b.Unsigned("obstacle", u->obstacle);
    b.Float("heightOffset", u->heightOffset);
    b.Int("sizeClass", u->sizeClass);
    b.Int("fightAtWill", u->fightAtWill);
    const UnitMotion* m = u->Motion();
    b.Int("motion.points", m->points.GetSize());
    b.Int("motion.waypoints", m->waypoints.GetSize());
    b.Int("motion.segment", m->segment);
    // The trajectory only while the unit has one: a unit that never moved keeps the bytes its
    // allocation left there (zero in a run of the original code alone, heap leftovers in the
    // image, whose allocations differ); a path start sets them (assumed: not checked
    // that nothing reads them before).
    if (m->points.GetSize() == 0 && m->segment < 0) {
        ReadController(u, b);
        return;
    }
    b.Float("motion.speedScale", m->speedScale);
    b.Float("motion.turnRate", m->turnRate);
    b.Float("motion.turn", m->turn);
    b.Float("motion.from.x", m->fromX);
    b.Float("motion.from.y", m->fromY);
    b.Float("motion.to.x", m->toX);
    b.Float("motion.to.y", m->toY);
    b.Float("motion.length", m->length);
    b.Float("motion.speed", m->speed);
    b.Float("motion.segmentStart", m->segmentStart);
    b.Float("motion.segmentEnd", m->segmentEnd);
    ReadController(u, b);
}

// A unit's controller (a CUnitServer, class 0x3E) and its AI brain.
void ReadController(CUnit* u, Builder& b) {
    CEngineObject* controller = u->controller;
    if (!controller || controller->GetClassId() != kClassUnitServer) {
        b.Int("controller", controller ? controller->GetClassId() : -1);
        return;
    }
    auto* c = static_cast<CUnitServer*>(controller);
    b.Int("state", c->state);
    b.Unsigned("busy", c->Busy());
    b.Unsigned("inCombat", c->orderStopped);
    b.Int("command", c->action.kind);
    b.Int("commandTarget", IdOf(c->action.target));
    b.Int("nextCommand", c->action2.kind);
    b.Int("nextTarget", IdOf(c->action2.target));
    b.Float("experience", c->stats.experience);
    b.Float("speed", c->stats.speed);
    b.Bits("statFlags", c->stats.flags);
    for (int i = 0; i < 4; ++i) b.Float("moveFactor#", c->moveFactors[i], i);
    ReadBrain(c->brain, b);
}

// A world object: the engine object's fields, a map object's place, effects and vitals, a unit's own.
void ReadObject(CEngineObject* o, bool mapObject, const std::map<const void*, int>& players, Builder& b) {
    const int classId = o->GetClassId();
    b.Int("class", classId);
    b.Bits("flags", o->flags_);
    b.Int("type", o->type_);
    b.Unsigned("prototype", o->prototypeId_);
    if (!mapObject) return;
    auto* m = static_cast<CMapObject*>(o);
    b.Vector("pos.x", "pos.y", "pos.z", m->position);
    b.Float("rot.w", m->orientation.w);
    b.Float("rot.x", m->orientation.x);
    b.Float("rot.y", m->orientation.y);
    b.Float("rot.z", m->orientation.z);
    b.Vector("scale.x", "scale.y", "scale.z", m->scale);
    b.Int("updateCountdown", m->updateCountdown);
    b.Float("animationSpeed", m->animationSpeed);
    b.Int("cell.x", m->sectorCellX);
    b.Int("cell.y", m->sectorCellY);
    ReadEffects(m, b);
    if (m->companion && m->companion->GetClassId() == kClassMapObjectServer) {
        const UnitVitals& v = static_cast<CMapObjectServer*>(m->companion)->vitals;
        b.Float("hp", v.hitPoints);
        b.Float("maxHp", v.maxHitPoints);
        b.Float("mana", v.mana);
        b.Float("maxMana", v.maxMana);
        b.Int("regenTicks", v.regenTicks);
    }
    if (classId == kClassUnit) ReadUnit(static_cast<CUnit*>(m), players, b);
}

std::string Describe(CEngineObject* o, bool mapObject) {
    std::string text = std::to_string(o->GetClassId());
    if (!mapObject) return text + " " + Text(o->GetClassName());
    auto* m = static_cast<CMapObject*>(o);
    text += std::string(" ") + Text(m->name);
    if (o->GetClassId() == kClassUnit) text += std::string(" \"") + Text(static_cast<CUnit*>(m)->scriptName) + "\"";
    return text;
}

// --- Digest 2 (SV2a): the timed objects, missiles, AI events, the script and the client world ---

std::uint32_t FloatBits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

std::uint32_t TextHash(const char* text) {
    Hash h;
    h.AddText(text);
    return h.h;
}

// A timed object's own state (the effects of world_effect.h, server side): the base's position,
// velocity and start tick, then the class's own fields. False for a class not read here.
bool ReadTimed(CEngineObject* o, Builder& b) {
    const int classId = o->GetClassId();
    switch (classId) {
    case kClassSound: case kClassAmbientSound: case kClassParticle: case kClassPointLight: case kClassLightning:
    case kClassArrow: case kClassWall: case kClassArrowFigure: case kClassShowVisibility: case kClassSoundSeq:
    case kClassTornado:
        break;
    default:
        return false;
    }
    auto* e = static_cast<CEffect*>(static_cast<CTransformObject*>(o));
    b.Vector("pos.x", "pos.y", "pos.z", e->localPosition_);
    b.Int("startTick", e->startTick_);
    b.Int("moving", e->moving_);
    b.Vector("velocity.x", "velocity.y", "velocity.z", e->velocity_);
    switch (classId) {
    case kClassSound: {
        auto* sound = static_cast<CEffectSound*>(e);
        b.Float("volume", sound->volume_);
        b.Float("radius", sound->radius_);
        b.Int("looping", sound->looping_);
        break;
    }
    case kClassParticle:
    case kClassWall: {
        auto* particle = static_cast<CEffectParticle*>(e);
        b.Int("particleType", particle->type_);
        b.Float("size", particle->size_);
        b.Int("attachedTo", particle->attachedTo_);
        b.Vector("target.x", "target.y", "target.z", particle->target_);
        b.Int("unit", particle->unitId_);
        break;
    }
    case kClassPointLight: {
        auto* light = static_cast<CEffectPointLight*>(e);
        b.Vector("color.x", "color.y", "color.z", light->color_);
        b.Float("range", light->range_);
        break;
    }
    case kClassLightning: {
        auto* bolt = static_cast<CEffectLightning*>(e);
        b.Vector("from.x", "from.y", "from.z", bolt->from_);
        b.Vector("to.x", "to.y", "to.z", bolt->to_);
        b.Int("fromObject", bolt->fromObject_);
        b.Int("toObject", bolt->toObject_);
        break;
    }
    case kClassArrow:
    case kClassArrowFigure: {
        auto* missile = static_cast<CEffectMissile*>(e);
        b.Int("arrived", missile->arrived_);
        b.Int("target", missile->targetId_);
        break;
    }
    default:
        break;
    }
    return true;
}

// A missile of the server's list (a CSpellEffect, spell.h: a spell on its way or lasting). Its
// visual (the flying CEffectArrow, a wall, a cloud) is a timed object: "timed<visual>".
std::string DescribeMissile(const Projectile* p) {
    const auto* e = reinterpret_cast<const CSpellEffect*>(p);
    std::uint32_t vtable;
    std::memcpy(&vtable, e->header, 4);
    if (vtable != CSpellEffect::kVtableAddress) {
        char text[24];
        std::snprintf(text, sizeof(text), "vtable 0x%08X", vtable);
        return text;
    }
    return "spell " + std::to_string(e->spellId >> 16);
}

void ReadMissile(const Projectile* p, Builder& b) {
    const auto* e = reinterpret_cast<const CSpellEffect*>(p);
    std::uint32_t vtable;
    std::memcpy(&vtable, e->header, 4);
    if (vtable != CSpellEffect::kVtableAddress) return;   // not a CSpellEffect: its description says so
    b.Int("state", e->state);
    b.Bits("flags", e->flags);
    b.Vector("pos.x", "pos.y", "pos.z", e->position);
    b.Int("target", IdOf(e->target));
    b.Int("caster", IdOf(e->caster));
    b.Vector("targetPoint.x", "targetPoint.y", "targetPoint.z", e->targetPoint);
    b.Int("timer", e->timer);
    b.Float("damage", e->damage);
    b.Float("radius", e->radius);
    b.Int("targets", e->targetCount);
    b.Int("visual", IdOf(e->visual));
    b.Int("sound", IdOf(e->sound));
    b.Int("light", IdOf(e->light));
    b.Unsigned("footprint", e->footprintStamped);
}

// The AI event table (CWorldServer + 0x2838): its tick, grid and, per side, the count and a hash of
// the events in wheel order (x, y, the origin, expiry, source and side). Left out: each event's
// payload (+0x14), which 005C9630 leaves as the free list's or the stack's leftovers.
void ReadEvents(const AIEventTable* t, Builder& b) {
    b.Int("tick", t->tick);
    b.Int("width", t->width);
    b.Int("height", t->height);
    Hash sides[16];
    int counts[16] = {};
    int total = 0;
    for (int slot = 0; slot < 0x400; ++slot) {
        for (int side = 0; side < 16; ++side) {
            for (const AIEvent* e = t->slots[slot][side]; e; e = e->nextInSlot) {
                Hash& h = sides[side];
                h.Add(FloatBits(e->x));
                h.Add(FloatBits(e->y));
                h.Add(e->origin[0]);
                h.Add(e->origin[1]);
                h.Add(static_cast<std::uint32_t>(e->expiry));
                h.Add(static_cast<std::uint32_t>(e->sourceId));
                h.Add(static_cast<std::uint32_t>(e->side));
                ++counts[side];
                ++total;
            }
        }
    }
    b.Int("count", total);
    for (int side = 0; side < 16; ++side) {
        b.Int("side#.count", counts[side], side);
        b.Bits("side#.hash", sides[side].h, side);
    }
}

// A script variable set: the floats' bits, the strings, the objects by id, the groups' ids (sorted:
// a group is kept in address order).
std::uint32_t HashVars(const ScriptVars& v) {
    Hash h;
    h.Add(static_cast<std::uint32_t>(v.floats.GetSize()));
    for (int i = 0; i < v.floats.GetSize(); ++i) h.Add(FloatBits(v.floats[i]));
    h.Add(static_cast<std::uint32_t>(v.strings.GetSize()));
    for (int i = 0; i < v.strings.GetSize(); ++i) h.AddText(v.strings[i]);
    h.Add(static_cast<std::uint32_t>(v.objects.GetSize()));
    for (int i = 0; i < v.objects.GetSize(); ++i) h.Add(static_cast<std::uint32_t>(IdOf(v.objects[i].object)));
    h.Add(static_cast<std::uint32_t>(v.groups.GetSize()));
    for (int i = 0; i < v.groups.GetSize(); ++i) {
        const ScriptGroup& group = v.groups[i];
        std::vector<int> ids;
        for (int j = 0; j < group.GetSize(); ++j) ids.push_back(IdOf(group[j].object));
        std::sort(ids.begin(), ids.end());
        h.Add(static_cast<std::uint32_t>(ids.size()));
        for (int id : ids) h.Add(static_cast<std::uint32_t>(id));
    }
    return h.h;
}

// The mission script (CScript, script.h): its running instances and waiting runs (the command
// queues: block, next command, ticks, sleep), the global variables (floats by name, the rest as
// hashes), and the effects it keeps with their countdowns.
void ReadScript(CScript* s, Builder& b) {
    b.Int("loaded", s->loaded_);
    b.Int("files", s->fileCount_);
    b.Int("nextParticleId", s->nextParticleId_);
    b.Int("nextFxId", s->nextFxId_);
    b.Int("instances", s->instances_.GetCount());
    std::vector<const int*> killFlags;
    int i = 0;
    for (auto* n = s->instances_.head; n; n = n->next, ++i) {
        killFlags.push_back(&n->data.index);
        b.Int("inst#.killed", n->data.index, i);
        b.Bits("inst#.name", TextHash(n->data.name), i);
        b.Bits("inst#.vars", HashVars(n->data.vars), i);
    }
    b.Int("runs", s->runs_.GetCount());
    i = 0;
    for (auto* n = s->runs_.head; n; n = n->next, ++i) {
        const ScriptRun& run = n->data;
        int instance = -1;   // the WorldScript's runs have none
        if (run.killFlag) {
            auto found = std::find(killFlags.begin(), killFlags.end(), run.killFlag);
            instance = found != killFlags.end() ? static_cast<int>(found - killFlags.begin()) : -2;
        }
        b.Int("run#.block", run.block ? run.block->id : -1, i);
        b.Int("run#.index", run.index, i);
        b.Int("run#.ticks", run.ticks, i);
        b.Int("run#.sleepStart", run.sleepStart, i);
        b.Int("run#.instance", instance, i);
        b.Bits("run#.vars", HashVars(run.vars), i);
    }
    // The global variables by name (by index past the names, "<kind>#"): floats, strings (a hash of the
    // text), objects (the id) and groups (a hash of the sorted ids).
    const ScriptVars& g = s->globals_;
    auto field = [&b](const char* kind, const mfc::CArray<mfc::CString>& names, int v, Kind type, std::uint32_t value) {
        if (v < names.GetSize()) b.Add(Intern(std::string(kind) + "." + static_cast<const char*>(names[v])), -1, type, value);
        else b.Add(Intern(std::string(kind) + "#"), v, type, value);
    };
    for (int v = 0; v < g.floats.GetSize(); ++v) field("global", g.floatNames, v, Kind::Float, FloatBits(g.floats[v]));
    for (int v = 0; v < g.strings.GetSize(); ++v) field("gstring", g.stringNames, v, Kind::Bits, TextHash(g.strings[v]));
    for (int v = 0; v < g.objects.GetSize(); ++v)
        field("gobject", g.objectNames, v, Kind::Int, static_cast<std::uint32_t>(IdOf(g.objects[v].object)));
    for (int v = 0; v < g.groups.GetSize(); ++v) {
        const ScriptGroup& group = g.groups[v];
        std::vector<int> ids;
        for (int j = 0; j < group.GetSize(); ++j) ids.push_back(IdOf(group[j].object));
        std::sort(ids.begin(), ids.end());
        field("ggroup", g.groupNames, v, Kind::Bits, HashWords(reinterpret_cast<const std::uint32_t*>(ids.data()), int(ids.size())));
    }
    b.Int("globals.names", g.floatNames.GetSize() + g.stringNames.GetSize() + g.objectNames.GetSize() + g.groupNames.GetSize());
    i = 0;
    for (auto* n = s->fxSources_.head; n; n = n->next, ++i) {
        const ScriptFxSource& fx = n->data;
        b.Int("fx#.id", fx.id, i);
        b.Int("fx#.sound", IdOf(fx.object), i);
        b.Float("fx#.x", fx.position.x, i);
        b.Float("fx#.y", fx.position.y, i);
        b.Float("fx#.z", fx.position.z, i);
        b.Int("fx#.idle", fx.idle, i);
        // A source CreateFXSource made with a new id has the original's stack leftovers in its
        // delays, spread and countdown (ours: 0, 0, 0, INT_MAX; docs/notes/script.md): only the
        // randomized sources' (idle) are read.
        if (!fx.idle) continue;
        b.Int("fx#.countdown", fx.countdown, i);
        b.Int("fx#.minDelay", fx.minDelay, i);
        b.Int("fx#.maxDelay", fx.maxDelay, i);
        b.Float("fx#.spread", fx.spread, i);
    }
    i = 0;
    for (auto* n = s->particles_.head; n; n = n->next, ++i) {
        const ScriptParticleSource& p = n->data;
        b.Int("part#.id", p.id, i);
        b.Int("part#.object", IdOf(p.object), i);
        b.Int("part#.moving", p.moving, i);
        b.Int("part#.steps", p.steps, i);
        b.Int("part#.lifetime", p.lifetime, i);
    }
    for (int slot = 0; slot < s->pointLights_.GetSize(); ++slot) {
        const ScriptPointLight& light = s->pointLights_[slot];
        if (!light.object) continue;
        b.Int("light#.id", light.id, slot);
        b.Int("light#.object", IdOf(light.object), slot);
        b.Int("light#.moving", light.moving, slot);
        b.Int("light#.steps", light.steps, slot);
    }
    Hash bolts;
    for (auto* n = s->lightning_.head; n; n = n->next) bolts.Add(static_cast<std::uint32_t>(IdOf(n->data.object)));
    b.Int("lightning", s->lightning_.GetCount());
    b.Bits("lightning.ids", bolts.h);
}

// The client world (CWorldClient, the drawing side): its clock and counts; its units go to "cunit<id>".
void ReadClientWorld(CWorldClient* c, Builder& b, std::vector<CUnit*>& units) {
    b.Int("tick", c->tick_);
    b.Unsigned("paused", c->paused_);
    b.Int("ticksRun", c->ticksRun_);
    b.Float("tickFraction", c->tickFraction_);
    b.Int("partyDanger", c->partyDanger_);
    int objects = 0;
    if (c->objects_.head) {
        for (auto* n = c->objects_.First(); n != c->objects_.End(); n = n->next) {
            ++objects;
            if (n->data && n->data->GetClassId() == kClassUnit) units.push_back(static_cast<CUnit*>(n->data));
        }
    }
    b.Int("objects", objects);
    b.Int("units", static_cast<int>(units.size()));
    b.Int("synchronised", c->dynamicObjects_.GetCount());
}

// A unit of the client world: what the screen shows (place, animation, the packed vitals).
void ReadClientUnit(CUnit* u, Builder& b) {
    b.Bits("flags", u->flags_);
    b.Vector("pos.x", "pos.y", "pos.z", u->position);
    b.Float("rot.w", u->orientation.w);
    b.Float("rot.x", u->orientation.x);
    b.Float("rot.y", u->orientation.y);
    b.Float("rot.z", u->orientation.z);
    b.Int("animation", u->animationEntry);
    b.Int("animation2", u->secondEntry);
    b.Int("animationStart", u->animationStart);
    b.Float("animationSpeed", u->animationSpeed);
    b.Int("dead", u->dead);
    const UnitVitalsPacked& v = u->packedVitals;
    b.Int("hp", v.hitPoints);
    b.Int("maxHp", v.maxHitPoints);
    b.Unsigned("mana", v.mana);
    b.Unsigned("maxMana", v.maxMana);
    b.Int("effects", u->effectCount);
    b.Int("controller", u->controller ? u->controller->GetClassId() : -1);
}

// The whole state, and the section hashes (one per group).
struct Snapshot {
    State state;
    std::uint32_t sections[kGroups] = {};
    std::uint32_t all = 0;
    std::string zone;
};

void Read(CWorldServer* w, Snapshot& s) {
    Builder b;
    b.entity = &s.state[{kWorld, 0}];
    b.entity->description = "world";
    ReadWorld(w, b);
    b.entity = &s.state[{kRandom, 0}];
    b.entity->description = "random";
    ReadRandom(b);
    s.zone = Text(w->zoneName_);

    std::map<const void*, int> players;
    if (w->players_) {
        PlayerIterator it;
        for (WorldPlayer* p = it.First(w->players_); p; p = it.Next()) {
            players[p] = p->id;
            b.entity = &s.state[{kPlayer, p->id}];
            b.entity->description = "player";
            ReadPlayer(p, b);
        }
    }

    // Map objects are those in the zone's maps (units, levers, traps, torches; the rest after the
    // zone's start in sentObjects_); other world objects (spells, effects, ...) get the base fields.
    std::set<unsigned> mapObjects;
    for (const ObjectIdMap* map : {&w->mapObjects_, &w->sentObjects_, &w->otherMapObjects_})
        map->map.ForEach([&](const auto* n) { mapObjects.insert(n->key); });
    std::vector<std::pair<unsigned, CEngineObject*>> objects;
    w->objects_.map.ForEach([&](const auto* n) {
        if (n->value) objects.emplace_back(n->key, n->value);
    });
    std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    for (const auto& [id, object] : objects) {
        const bool mapObject = mapObjects.count(id) != 0;
        b.entity = &s.state[{kObject, static_cast<int>(id)}];
        b.entity->description = Describe(object, mapObject);
        ReadObject(object, mapObject, players, b);
    }

    // The timed objects' own state (the effects), by id.
    std::vector<std::pair<unsigned, CEngineObject*>> timed;
    w->timedObjects_.map.ForEach([&](const auto* n) {
        if (n->value) timed.emplace_back(n->key, n->value);
    });
    std::sort(timed.begin(), timed.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    for (const auto& [id, object] : timed) {
        Entity entity;
        entity.description = std::to_string(object->GetClassId()) + " " + Text(object->GetClassName());
        b.entity = &entity;
        if (ReadTimed(object, b)) s.state[{kTimed, static_cast<int>(id)}] = std::move(entity);
    }

    // The missiles by their place in the list.
    int place = 0;
    for (auto* n = w->projectiles_.head; n; n = n->next, ++place) {
        if (!n->data) continue;
        b.entity = &s.state[{kMissile, place}];
        b.entity->description = DescribeMissile(n->data);
        ReadMissile(n->data, b);
    }

    b.entity = &s.state[{kEvents, 0}];
    b.entity->description = "events";
    ReadEvents(w->AIEvents(), b);

    b.entity = &s.state[{kScript, 0}];
    b.entity->description = "script";
    ReadScript(MissionScriptObject(), b);

    std::vector<CUnit*> clientUnits;
    b.entity = &s.state[{kClient, 0}];
    b.entity->description = "client";
    ReadClientWorld(ClientWorldObject(), b, clientUnits);
    for (CUnit* u : clientUnits) {
        b.entity = &s.state[{kClientUnit, u->id_}];
        b.entity->description = Describe(u, true);
        ReadClientUnit(u, b);
    }

    Hash all, section[kGroups];
    for (const auto& [key, entity] : s.state) {
        Hash& h = section[key.first];
        h.Add(static_cast<std::uint32_t>(key.second));
        for (const Field& f : entity.fields) h.Add(f.value);
        h.Add(static_cast<std::uint32_t>(entity.fields.size()));
    }
    for (int g = 0; g < kGroups; ++g) {
        s.sections[g] = section[g].h;
        all.Add(s.sections[g]);
    }
    all.AddText(s.zone.c_str());
    s.all = all.h;
}

// --- Writing ---

std::string FieldName(const Field& f) {
    std::string name = f.name;
    if (f.index >= 0) {
        std::size_t at = name.find('#');
        if (at != std::string::npos) name.replace(at, 1, std::to_string(f.index));
    }
    return name;
}

std::string FieldValue(const Field& f) {
    char text[40];
    switch (f.kind) {
    case Kind::Int: std::snprintf(text, sizeof(text), "%d", static_cast<int>(f.value)); break;
    case Kind::Unsigned: std::snprintf(text, sizeof(text), "%u", f.value); break;
    case Kind::Bits: std::snprintf(text, sizeof(text), "0x%08X", f.value); break;
    case Kind::Mode: {
        // An AI mode by name, one ModeName does not know by its vtable (none: 0).
        const char* mode = ModeName(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(f.value)));
        if (mode) std::snprintf(text, sizeof(text), "%s", mode);
        else std::snprintf(text, sizeof(text), "0x%08X", f.value);
        break;
    }
    case Kind::Float: {
        float v;
        std::memcpy(&v, &f.value, 4);
        // A bit test (rewrite guide rule 7: /fp:fast folds the comparisons).
        if ((f.value & 0x7F800000u) == 0x7F800000u && (f.value & 0x007FFFFFu)) {
            std::snprintf(text, sizeof(text), "nan:0x%08X", f.value);   // which NaN, too
        } else {
            std::snprintf(text, sizeof(text), "%.9g", v);
            if (!std::strpbrk(text, ".eni")) strcat_s(text, ".0");   // a float always shows it is one
        }
        break;
    }
    }
    return text;
}

void WriteField(FILE* f, const std::string& key, const Field& field) {
    std::fprintf(f, "%s %s %s\n", key.c_str(), FieldName(field).c_str(), FieldValue(field).c_str());
}

void WriteHeader(FILE* f, unsigned record, unsigned update, unsigned frame, int tick, const Snapshot& s) {
    const std::uint32_t* h = s.sections;
    std::fprintf(f, "@ %u update %u frame %u tick %d zone %s hash %08X world %08X random %08X players %08X objects %08X "
                    "timed %08X missiles %08X events %08X script %08X client %08X n %u\n",
                 record, update, frame, tick, s.zone.empty() ? "-" : s.zone.c_str(), s.all, h[kWorld], h[kRandom], h[kPlayer],
                 h[kObject], h[kTimed], h[kMissile], h[kEvents], h[kScript], h[kClient] ^ h[kClientUnit],
                 unsigned(s.state.size()));
}

// What changed from `before` to `now`, in record order.
void WriteDelta(FILE* f, const State& before, const State& now) {
    for (const auto& [key, entity] : now) {
        const std::string name = KeyName(key);
        auto old = before.find(key);
        if (old == before.end() || old->second.description != entity.description) {
            if (old != before.end()) std::fprintf(f, "- %s\n", name.c_str());
            std::fprintf(f, "+ %s %s\n", name.c_str(), entity.description.c_str());
            for (const Field& field : entity.fields) WriteField(f, name, field);
            continue;
        }
        const std::vector<Field>& a = old->second.fields;
        const std::vector<Field>& c = entity.fields;
        bool sameLayout = a.size() == c.size();
        for (std::size_t i = 0; sameLayout && i < a.size(); ++i) sameLayout = SameName(a[i], c[i]);
        if (sameLayout) {
            for (std::size_t i = 0; i < c.size(); ++i)
                if (a[i].value != c[i].value || a[i].kind != c[i].kind) WriteField(f, name, c[i]);
            continue;
        }
        for (const Field& field : c) {
            auto it = std::find_if(a.begin(), a.end(), [&](const Field& x) { return SameName(x, field); });
            if (it == a.end() || it->value != field.value || it->kind != field.kind) WriteField(f, name, field);
        }
        for (const Field& field : a)
            if (std::none_of(c.begin(), c.end(), [&](const Field& x) { return SameName(x, field); }))
                std::fprintf(f, "%s %s -\n", name.c_str(), FieldName(field).c_str());
    }
    for (const auto& [key, entity] : before)
        if (!now.count(key)) std::fprintf(f, "- %s\n", KeyName(key).c_str());
}

// --- The digest's state ---

const unsigned* g_frames = nullptr;
unsigned g_period = 0;          // world updates between records (0: none)
unsigned g_updates = 0;         // world updates seen
unsigned g_nextAt = 0;          // the update count of the next periodic record
unsigned g_records = 0;
std::uint32_t g_lastHash = 0;
CWorldServer* g_server = nullptr;
FILE* g_file = nullptr;
State g_previous;

FILE* OpenOutput(const char* name, const char* mode) {
    char path[MAX_PATH];
    ei::OutputPath(name, path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, mode) != 0) return nullptr;
    return f;
}

bool OpenDigest() {
    if (g_file) return true;
    g_file = OpenOutput("digest.txt", "w");
    if (!g_file) return false;
    std::setvbuf(g_file, nullptr, _IOFBF, 1 << 20);
    char hooks[8] = "";
    GetEnvironmentVariableA("EI_NO_HOOKS", hooks, sizeof(hooks));
    std::fprintf(g_file, "# ei state digest 2; groups %s; hooks %s; install %s; every %u world updates\n", kNewGroups,
                 hooks[0] == '1' ? "off" : "on", starter::RebirthInstall() ? "rebirth" : "vanilla", g_period);
    return true;
}

void TakeRecord() {
    if (!g_server || !OpenDigest()) return;
    Snapshot s;
    Read(g_server, s);
    WriteHeader(g_file, g_records, g_updates, g_frames ? *g_frames : 0, g_server->tick_, s);
    WriteDelta(g_file, g_previous, s.state);
    clock::RandomNote("digest %u update %u hash %08X", g_records, g_updates, s.all);
    ++g_records;
    g_lastHash = s.all;
    g_previous = std::move(s.state);
}

} // namespace

void Init(const unsigned* frames) {
    g_frames = frames;
    char value[32] = "";
    if (GetEnvironmentVariableA("EI_DIGEST", value, sizeof(value))) {
        g_period = std::strtoul(value, nullptr, 10);
        g_nextAt = g_period;
        if (g_period) Log("harness: state digest every %u world updates (EI_DIGEST)", g_period);
    }
}

namespace {

// The heap trace's end: every world object's id, class and address.
void NoteAddresses(CWorldServer* w) {
    std::vector<std::pair<unsigned, CEngineObject*>> objects;
    w->objects_.map.ForEach([&](const auto* n) {
        if (n->value) objects.emplace_back(n->key, n->value);
    });
    std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    char line[96];
    for (const auto& [id, object] : objects) {
        std::snprintf(line, sizeof(line), "obj %u %d %08X", id, object->GetClassId(), heap::TracePlace(object));
        heap::Note(line);
    }
}

} // namespace

void AfterWorldUpdate(void* server) {
    g_server = static_cast<CWorldServer*>(server);
    ++g_updates;
    if (heap::TraceUntil() && g_updates == heap::TraceUntil()) {
        NoteAddresses(g_server);
        heap::StopTrace();
    }
    if (g_period && g_updates >= g_nextAt) {
        g_nextAt = g_updates + g_period;
        TakeRecord();
    }
}

std::string Step(const std::string& argument) {
    if (argument.empty() || argument == "now") {
        if (!g_server) return "digest: no world update has run yet";
        TakeRecord();
        return "";
    }
    if (argument == "off") {
        g_period = 0;
        return "";
    }
    char* end = nullptr;
    unsigned long period = std::strtoul(argument.c_str(), &end, 10);
    if (!end || *end) return "digest: '" + argument + "' is not a period (world updates), off or now";
    g_period = period;
    g_nextAt = g_updates + period;
    Log("harness: state digest every %u world updates", g_period);
    return "";
}

std::string Dump(const std::string& name) {
    bool valid = !name.empty() && name.size() < 64;
    for (char c : name) valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.');
    if (!valid) return "dump: bad name '" + name + "'";
    if (!g_server) return "dump: no world update has run yet";
    FILE* f = OpenOutput(("dump_" + name + ".txt").c_str(), "w");
    if (!f) return "dump: cannot write dump_" + name + ".txt";
    Snapshot s;
    Read(g_server, s);
    std::fprintf(f, "# ei state digest 2; groups %s; dump %s\n", kNewGroups, name.c_str());
    WriteHeader(f, g_records, g_updates, g_frames ? *g_frames : 0, g_server->tick_, s);
    WriteDelta(f, State{}, s.state);
    std::fclose(f);
    Log("harness: dump %s: hash %08X, %u entities", name.c_str(), s.all, unsigned(s.state.size()));
    return "";
}

std::string Summary() {
    if (!g_records) return "";
    char text[120];
    std::snprintf(text, sizeof(text), "%u records in digest.txt, the last after world update %u: hash %08X", g_records,
                  g_updates, g_lastHash);
    return text;
}

void Flush() {
    if (g_file) std::fflush(g_file);
}

} // namespace ei::verify::digest
