// The digest's reading code (harness_digest.h), included once by each reader inside its own namespace, where the
// game's class names are the objects it reads: game.exe's views (harness_digest_exe.cpp, namespace exe_side) or our
// classes (image/harness_digest_image.cpp, image_side). Before it each reader defines, for its objects:
//   CWorldClient* ClientWorld()                          the client world
//   void ReadRandom(Builder& b)                          the generators' state ("random")
//   std::uint32_t ModeId(AIMode* mode)                   an AI mode's id (ModeKindId; 0 for none)
//   bool IsSpellEffect(const CSpellEffect* e)            whether a missile is a cast spell
//   std::uint32_t MissileVtable(const CSpellEffect* e)   a missile's table as game.exe's address
// The standard headers it uses come first (harness_digest_read.h). No pointers go into a record: objects are named by
// their ids.

namespace {

int IdOf(const CEngineObject* object) { return object ? object->id_ : -1; }

// The players of a player list in its order: a plain walk of its nodes.
std::vector<CPlayer*> PlayersOf(CPlayerManager* list) {
    std::vector<CPlayer*> players;
    for (auto* node = list->objectList_.head; node; node = node->next) players.push_back(static_cast<CPlayer*>(node->data));
    return players;
}

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
    b.Bits("nightLevel", std::bit_cast<std::uint32_t>(w->nightLevel_));
    b.Bits("weatherFactor", w->weatherFactor_);
    for (int i = 0; i < 5; ++i) {
        const auto& a = w->alarms_[i];
        b.Unsigned("alarm#.active", a.active, i);
        b.Int("alarm#.time", a.time, i);
        b.Float("alarm#.x", a.x, i);
        b.Float("alarm#.y", a.y, i);
    }
    b.Int("climate", w->climate_);
    b.Int("cave", w->cave_);
    b.Int("weather", w->weather_);
    b.Float("weatherStrength", w->weatherStrength_);
    b.Int("exits", w->exits_.size);
    b.Int("gameResult", w->gameResult_);
    b.Unsigned("lastRainBoltTick", w->lastRainBoltTick_);
    b.Int("zoneLoaded", w->zoneLoaded_);
    b.Int("scriptsActive", w->scriptsActive_);
    b.Vector("start.x", "start.y", "start.z", w->startPosition_);
    b.Int("occluderReplanTime", w->AIMap()->footprintTick);
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

void ReadPlayer(CPlayer* p, Builder& b) {
    b.Int("id", p->playerId_);
    b.Unsigned("partyId", p->partyId_);
    b.Unsigned("sessionKind", p->sessionKind100_);
    b.Int("lastScreen", p->lastScreen_);
    b.Unsigned("godMode", p->godMode_);
    b.Int("lastTick", p->lastTick_);
    b.Int("items", p->items_.GetCount());
    b.Int("ids", p->recharging_.GetCount());
    b.Unsigned("units", static_cast<std::uint32_t>(p->units_.Size()));
    b.Unsigned("objects", static_cast<std::uint32_t>(p->knownObjects_.Size()));
    CParty* party = p->Party();
    b.Unsigned("money", party->money_);
    b.Int("partyItems", party->items_.size);
    b.Int("members", party->members_.GetSize());
    for (int i = 0; i < party->members_.GetSize(); ++i) b.Unsigned("member#.unit", party->members_[i].unitId, i);
    b.Unsigned("entrance", party->entrance_);
    std::vector<std::pair<std::string, float>> variables;
    for (const auto* n = party->variables_.First(); n; n = party->variables_.Next(n))
        variables.emplace_back(Text(n->value.key), n->value.mapped);
    std::sort(variables.begin(), variables.end());
    for (const auto& v : variables) b.Float(Intern("var." + v.first), v.second);
}

void ReadEffects(CMapObject* m, Builder& b) {
    b.Int("effects", m->effects.size);
    for (int i = 0; i < m->effects.size; ++i) {
        const auto& e = m->effects.data[i];
        b.Unsigned("effect#.type", e.type, i);
        b.Unsigned("effect#.power", e.power, i);
        b.Int("effect#.ticksLeft", e.ticksLeft, i);
        b.Bits("effect#.value", e.value, i);
        b.Bits("effect#.strength", e.strength, i);
    }
}

void ReadBrain(AIBrain& brain, Builder& b) {
    b.Int("ai.action", brain.action);
    b.Vector("ai.point.x", "ai.point.y", "ai.point.z", brain.move.point);
    b.Int("ai.looks", brain.move.looks.GetSize());
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
    b.Float("ai.noticed.x", brain.memory.noticedX);
    b.Float("ai.noticed.y", brain.memory.noticedY);
    b.Int("ai.noticedLevel", brain.memory.noticedLevel);
    b.Int("ai.noticedKind", brain.memory.noticedKind);
    b.Int("ai.nearby", brain.memory.nearby.GetCount());
    b.Int("ai.visible", brain.memory.visible.GetCount());
    b.Int("ai.corpses", brain.memory.corpses.GetCount());
    b.Int("ai.reports", brain.memory.reports.GetSize());
    b.Int("ai.calls", brain.memory.calls.GetSize());
    b.Unsigned("ai.seesEnemy", brain.memory.seesEnemy);
    // The modes by their kinds' ids (ModeId: game.exe's vtable addresses; the writer names the known ones).
    b.Mode("ai.mode", ModeId(brain.current));
    int i = 0;
    for (auto* n = brain.modes.head; n; n = n->next) b.Mode("ai.modes#", ModeId(n->data), i++);
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
    const auto* m = u->Motion();
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

// A world object: the engine object's fields, a map object's place, effects and vitals, a unit's own.
void ReadObject(CEngineObject* o, bool mapObject, const std::map<const void*, int>& players, Builder& b) {
    const int classId = o->GetClassId();
    b.Int("class", classId);
    b.Bits("flags", o->flags_);
    b.Int("type", o->type_);
    b.Unsigned("prototype", o->prototypeId_);
    if (!mapObject) return;
    auto* m = static_cast<CMapObject*>(o);
    b.Vector("pos.x", "pos.y", "pos.z", m->localPosition_);
    b.Float("rot.w", m->localRotation_.w);
    b.Float("rot.x", m->localRotation_.x);
    b.Float("rot.y", m->localRotation_.y);
    b.Float("rot.z", m->localRotation_.z);
    b.Vector("scale.x", "scale.y", "scale.z", m->scale);
    b.Int("updateCountdown", m->updateCountdown);
    b.Float("animationSpeed", m->animationSpeed);
    b.Int("cell.x", m->sectorCellX);
    b.Int("cell.y", m->sectorCellY);
    ReadEffects(m, b);
    if (m->companion && m->companion->GetClassId() == kClassMapObjectServer) {
        const auto& v = static_cast<CMapObjectServer*>(m->companion)->vitals;
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
std::string DescribeMissile(const CSpellEffect* e) {
    if (!IsSpellEffect(e)) {
        char text[24];
        std::snprintf(text, sizeof(text), "vtable 0x%08X", unsigned(MissileVtable(e)));
        return text;
    }
    return "spell " + std::to_string(e->Spell()->prototypeId_ >> 16);
}

void ReadMissile(const CSpellEffect* e, Builder& b) {
    if (!IsSpellEffect(e)) return;   // not a CSpellEffect: its description says so
    b.Int("state", e->state);
    b.Bits("flags", e->flags);
    b.Vector("pos.x", "pos.y", "pos.z", e->position);
    b.Int("target", IdOf(e->target));
    b.Int("caster", IdOf(e->caster));
    b.Vector("targetPoint.x", "targetPoint.y", "targetPoint.z", e->targetPoint);
    b.Int("timer", e->timer);
    b.Float("damage", e->Spell()->damage_);
    b.Float("radius", e->Spell()->radius_);
    b.Int("targets", static_cast<int>(e->Spell()->duration_));
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
    h.Add(static_cast<std::uint32_t>(v.floats.values.GetSize()));
    for (int i = 0; i < v.floats.values.GetSize(); ++i) h.Add(FloatBits(v.floats.values[i]));
    h.Add(static_cast<std::uint32_t>(v.strings.values.GetSize()));
    for (int i = 0; i < v.strings.values.GetSize(); ++i) h.AddText(Text(v.strings.values[i]));
    h.Add(static_cast<std::uint32_t>(v.objects.values.GetSize()));
    for (int i = 0; i < v.objects.values.GetSize(); ++i) h.Add(static_cast<std::uint32_t>(IdOf(v.objects.values[i].object)));
    h.Add(static_cast<std::uint32_t>(v.groups.values.GetSize()));
    for (int i = 0; i < v.groups.values.GetSize(); ++i) {
        const ScriptGroup& group = v.groups.values[i];
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
        b.Bits("inst#.name", TextHash(Text(n->data.name)), i);
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
    auto field = [&b](const char* kind, const auto& names, int v, Kind type, std::uint32_t value) {
        if (v < names.GetSize()) b.Add(Intern(std::string(kind) + "." + Text(names[v])), -1, type, value);
        else b.Add(Intern(std::string(kind) + "#"), v, type, value);
    };
    for (int v = 0; v < g.floats.values.GetSize(); ++v) field("global", g.floats.names, v, Kind::Float, FloatBits(g.floats.values[v]));
    for (int v = 0; v < g.strings.values.GetSize(); ++v) field("gstring", g.strings.names, v, Kind::Bits, TextHash(Text(g.strings.values[v])));
    for (int v = 0; v < g.objects.values.GetSize(); ++v)
        field("gobject", g.objects.names, v, Kind::Int, static_cast<std::uint32_t>(IdOf(g.objects.values[v].object)));
    for (int v = 0; v < g.groups.values.GetSize(); ++v) {
        const ScriptGroup& group = g.groups.values[v];
        std::vector<int> ids;
        for (int j = 0; j < group.GetSize(); ++j) ids.push_back(IdOf(group[j].object));
        std::sort(ids.begin(), ids.end());
        field("ggroup", g.groups.names, v, Kind::Bits, HashWords(reinterpret_cast<const std::uint32_t*>(ids.data()), int(ids.size())));
    }
    b.Int("globals.names", g.floats.names.GetSize() + g.strings.names.GetSize() + g.objects.names.GetSize() + g.groups.names.GetSize());
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
    b.Vector("pos.x", "pos.y", "pos.z", u->localPosition_);
    b.Float("rot.w", u->localRotation_.w);
    b.Float("rot.x", u->localRotation_.x);
    b.Float("rot.y", u->localRotation_.y);
    b.Float("rot.z", u->localRotation_.z);
    b.Int("animation", u->animationEntry);
    b.Int("animation2", u->secondEntry);
    b.Int("animationStart", u->animationStart);
    b.Float("animationSpeed", u->animationSpeed);
    b.Int("dead", u->dead);
    const auto& v = u->packedVitals;
    b.Int("hp", v.hitPoints);
    b.Int("maxHp", v.maxHitPoints);
    b.Unsigned("mana", v.mana);
    b.Unsigned("maxMana", v.maxMana);
    b.Int("effects", u->effects.size);
    b.Int("controller", u->controller ? u->controller->GetClassId() : -1);
}

// The water layer as the waves leave it (SectorData::Animate 006C3E80, R16c3): the landscape's wave
// state, and per cached sector with water (by sx, sy) the vertices with a water material and hashes
// of their positions and speculars.
void ReadWater(State& state) {
    const auto* t = ClientWorld()->terrain_;
    if (!t) return;
    Builder b;
    b.entity = &Slot(state, {kWater, -1});
    b.entity->description = "landscape";
    b.Bits("flags", t->landscapeFlags);
    b.Int("frame", t->frame);
    b.Float("phase", t->wavePhase);
    constexpr int kVertices = 33 * 33;
    for (auto* node = t->sectorCache.head; node; node = node->next) {
        const auto* sector = node->data;
        if (!sector || !sector->hasWater || sector->vertices.size < 2 * kVertices || sector->records.size < 2 * kVertices)
            continue;
        Hash positions, speculars;
        int water = 0;
        for (int i = kVertices; i < 2 * kVertices; ++i) {
            const auto& record = sector->records[i];
            if (record.waterMaterial == 0xFFFF) continue;
            ++water;
            std::uint32_t bits[3];
            std::memcpy(bits, &sector->vertices[i], sizeof(bits));
            for (std::uint32_t word : bits) positions.Add(word);
            speculars.Add(record.specular);
        }
        b.entity = &Slot(state, {kWater, sector->sy * 1000 + sector->sx});
        b.entity->description = "sector " + std::to_string(sector->sx) + " " + std::to_string(sector->sy);
        b.Int("water", water);
        b.Bits("positions", positions.h);
        b.Bits("speculars", speculars.h);
    }
}

// The objects of a world map with an object, by id.
std::vector<std::pair<unsigned, CEngineObject*>> ObjectsById(const auto& objects) {
    std::vector<std::pair<unsigned, CEngineObject*>> sorted;
    objects.map.ForEach([&](const auto* n) {
        if (n->value) sorted.emplace_back(n->key, n->value);
    });
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    return sorted;
}

} // namespace

void Read(void* server, Snapshot& s) {
    auto* w = static_cast<CWorldServer*>(server);
    Builder b;
    b.entity = &Slot(s.state, {kWorld, 0});
    b.entity->description = "world";
    ReadWorld(w, b);
    b.entity = &Slot(s.state, {kRandom, 0});
    b.entity->description = "random";
    ReadRandom(b);
    s.zone = Text(w->zoneName_);
    s.tick = w->tick_;

    std::map<const void*, int> players;
    if (w->players_) {
        for (CPlayer* p : PlayersOf(w->players_)) {
            players[p] = p->playerId_;
            b.entity = &Slot(s.state, {kPlayer, p->playerId_});
            b.entity->description = "player";
            ReadPlayer(p, b);
        }
    }

    // Map objects are those in the zone's maps (units, levers, traps, torches; the rest after the
    // zone's start in sentObjects_); other world objects (spells, effects, ...) get the base fields.
    std::set<unsigned> mapObjects;
    for (const auto* map : {&w->mapObjects_, &w->sentObjects_, &w->otherMapObjects_})
        map->map.ForEach([&](const auto* n) { mapObjects.insert(n->key); });
    for (const auto& [id, object] : ObjectsById(w->objects_)) {
        const bool mapObject = mapObjects.count(id) != 0;
        b.entity = &Slot(s.state, {kObject, static_cast<int>(id)});
        b.entity->description = Describe(object, mapObject);
        ReadObject(object, mapObject, players, b);
    }

    // The timed objects' own state (the effects), by id.
    for (const auto& [id, object] : ObjectsById(w->timedObjects_)) {
        Entity entity;
        entity.description = std::to_string(object->GetClassId()) + " " + Text(object->GetClassName());
        b.entity = &entity;
        if (ReadTimed(object, b)) s.state[{kTimed, static_cast<int>(id)}] = std::move(entity);
    }

    // The missiles by their place in the list.
    int place = 0;
    for (auto* n = w->projectiles_.head; n; n = n->next, ++place) {
        if (!n->data) continue;
        b.entity = &Slot(s.state, {kMissile, place});
        b.entity->description = DescribeMissile(n->data);
        ReadMissile(n->data, b);
    }

    b.entity = &Slot(s.state, {kEvents, 0});
    b.entity->description = "events";
    ReadEvents(w->AIEvents(), b);

    b.entity = &Slot(s.state, {kScript, 0});
    b.entity->description = "script";
    ReadScript(w->MainScript(), b);

    std::vector<CUnit*> clientUnits;
    b.entity = &Slot(s.state, {kClient, 0});
    b.entity->description = "client";
    ReadClientWorld(ClientWorld(), b, clientUnits);
    for (CUnit* u : clientUnits) {
        b.entity = &Slot(s.state, {kClientUnit, u->id_});
        b.entity->description = Describe(u, true);
        ReadClientUnit(u, b);
    }
    if (WaterOn()) ReadWater(s.state);
}

UnitNow Unit(void* server, int id) {
    UnitNow now;
    CEngineObject* object = nullptr;
    static_cast<CWorldServer*>(server)->objects_.map.ForEach([&](const auto* n) {
        if (n->key == static_cast<unsigned>(id)) object = n->value;
    });
    if (!object || object->GetClassId() != kClassUnit) return now;
    auto* u = static_cast<CUnit*>(object);
    now.found = true;
    now.dead = u->dead != 0;
    CEngineObject* controller = u->controller;
    if (controller && controller->GetClassId() == kClassUnitServer) {
        auto* c = static_cast<CUnitServer*>(controller);
        now.idle = c->action.kind == 0 && u->Motion()->points.GetSize() == 0;
        now.experience = c->stats.experience;
    }
    return now;
}

int LocalLeader(void* server, unsigned* money) {
    auto* w = static_cast<CWorldServer*>(server);
    if (!w->players_) return -1;
    for (CPlayer* p : PlayersOf(w->players_)) {
        if (p->playerId_ != 0) continue;
        CParty* party = p->Party();
        if (money) *money = party->money_;
        return party->members_.GetSize() > 0 ? static_cast<int>(party->members_[0].unitId) : -1;
    }
    return -1;
}

// The heap trace's end: every world object's id, class and address.
void NoteAddresses(void* server) {
    char line[96];
    for (const auto& [id, object] : ObjectsById(static_cast<CWorldServer*>(server)->objects_)) {
        std::snprintf(line, sizeof(line), "obj %u %d %08X", id, object->GetClassId(), heap::TracePlace(object));
        heap::Note(line);
    }
}
