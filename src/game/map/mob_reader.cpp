// The .mob zone file reader: one function reads the top-level chunks, another turns the records into world
// objects. Names are provisional; the record classes are map_spec.h. The record loader has a branch under a
// global (a colour quantisation of the objects' tints, through STLport map / list / introsort
// instantiations). Nothing in the game, addon.dll or EI_Plugin.asi writes that global (it sits in
// zero-initialised data; a byte search finds its address only in the test), so the branch never runs and is
// not reproduced: see docs/notes/script.md.
#include "game/app/movies.h"

#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/map/map_spec.h"
#include "game/map/mob_script_decoder.h"
#include "game/objects/map_object_manager.h"
#include "game/world/diplomacy.h"
#include "game/data/options.h"
#include "game/script/script.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "lib/bytes.h"
#include "lib/float_select.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace mapspec {

namespace {

// Top-level chunk ids.
constexpr std::uint32_t kChunkScripts = 0xACCEECCA;
constexpr std::uint32_t kChunkEncodedScripts = 0xACCEECCB;
constexpr std::uint32_t kChunkWeather = 0xABD0;
constexpr std::uint32_t kChunkRecords = 0xB000;
constexpr std::uint32_t kChunkPlayers = 0xDDDDDDD1;
constexpr std::uint32_t kChunkFlagOn = 0xC000;
constexpr std::uint32_t kChunkFlagOff = 0xD000;

constexpr int kBehaviourCount = 5;

// A new record for the given chunk id, or null for other ids.
std::unique_ptr<Spec> NewRecord(std::uint32_t id) {
    switch (id) {
    case 0xB001: return std::make_unique<ObjectSpec>();
    case 0xAA01: return std::make_unique<LightSpec>();
    case 0xCC01: return std::make_unique<SoundSpec>();
    case 0xDD01: return std::make_unique<ParticleSpec>();
    case 0xBBAB0000: return std::make_unique<TrapSpec>();
    case 0xBBAC0000: return std::make_unique<LeverSpec>();
    case 0xBBBB0000: return std::make_unique<UnitSpec>();
    case 0xBBBF0000: return std::make_unique<FlameSpec>();
    default: return nullptr;
    }
}

// 0xB000: every record chunk inside `parent` is read into a new record.
void ReadRecords(Chunk* parent, MobRecords* records) {
    Chunk chunk(parent);
    do {
        chunk.ReadHeader();
        if (std::unique_ptr<Spec> spec = NewRecord(chunk.id)) {
            spec->parent = &chunk;
            spec->ForEachChild();
            records->Add(std::move(spec));
        }
    } while (parent->size != parent->consumed);
}

// 0xABD0: the wind's direction and strength and the time of day (two more values are read and unused).
MobWeather ReadWeather(Chunk* parent) {
    Chunk chunk(parent);
    MobWeather weather;
    chunk.ReadHeader();
    weather.wind = chunk.ReadVector3();
    chunk.ReadHeader();
    weather.windStrength = chunk.ReadFloat();
    chunk.ReadHeader();
    weather.time = chunk.ReadFloat();
    chunk.ReadHeader();
    chunk.ReadFloat();
    chunk.ReadHeader();
    chunk.ReadFloat();
    return weather;
}

// 0xDDDDDDD1: the relation table and the player names.
void ReadPlayers(Chunk* parent, ZoneDiplomacy* diplomacy) {
    Chunk chunk(parent);
    chunk.ReadHeader();
    chunk.ReadBytes(diplomacy->relations, sizeof(diplomacy->relations));
    chunk.ReadHeader();
    diplomacy->names = chunk.ReadStringArray();
}

// The scripts' text, plain or encoded.
void ReadScripts(Chunk* chunk, std::string* scripts, bool encoded) {
    *scripts = encoded ? chunk->ReadEncodedString() : chunk->ReadString();
}

// Whether the factory makes the record's class (the TOML's `class`, the chunk's parent id) for a record of its kind:
// the world casts the object it builds to the kind's type, so a class that makes none (a hand-written record without
// a class is 0) or another kind's would crash the zone's load. Kinds with no class of their own (lights, sounds,
// particles) always fit.
bool ClassFits(const Spec& spec) {
    if (spec.kind != SpecKind::Object && spec.kind != SpecKind::Unit && spec.kind != SpecKind::Flame &&
        spec.kind != SpecKind::Trap && spec.kind != SpecKind::Lever)
        return true;
    const auto type = static_cast<MapObjectType>(static_cast<const ObjectSpec&>(spec).parentId);
    switch (spec.kind) {
    case SpecKind::Unit: return IsUnitType(type);
    case SpecKind::Flame: return type == MapObjectType::Torch;
    case SpecKind::Trap: return type == MapObjectType::MagicTrap;
    case SpecKind::Lever: return type == MapObjectType::Lever;
    default:
        // An object record may hold a unit's class too (the main menu's unmoco2 is one).
        return IsUnitType(type) || type == MapObjectType::Tree || type == MapObjectType::Building ||
               type == MapObjectType::Stone || type == MapObjectType::StuffObject;
    }
}

} // namespace

void MobRecords::Add(std::unique_ptr<Spec> spec) {
    byId[spec->id] = spec.get();
    read.push_back(std::move(spec));
}

MobContents MobFile::ReadChunks(bool objects) {
    MobContents contents;
    ReadHeader();
    Chunk child(this);
    do {
        child.ReadHeader();
        const std::uint32_t childId = child.id;
        if (child.size != child.consumed) {
            switch (childId) {
            case kChunkScripts: ReadScripts(&child, &contents.scripts, false); break;
            case kChunkEncodedScripts: ReadScripts(&child, &contents.scripts, true); break;
            case kChunkWeather:
                if (objects) contents.weather.push_back(ReadWeather(&child));
                break;
            case kChunkRecords:
                if (objects) ReadRecords(&child, &contents.records);
                break;
            case kChunkPlayers:
                if (objects) ReadPlayers(&child, &contents.diplomacy);
                break;
            default: break;
            }
        }
        if (childId == kChunkFlagOn) contents.appliesRelations = true;
        else if (childId == kChunkFlagOff) contents.appliesRelations = false;
        child.SkipContent();
    } while (size != consumed);
    return contents;
}

void LoadMob(MobContents& contents, int keepIds, std::string_view name) {
    WorldServer* server = &g_serverWorld;
    for (const MobWeather& weather : contents.weather) {
        server->WorldServer::SetTime(weather.time);
        server->ChangeWind(weather.wind, MinKeepA(options::kDefaultWindForce, weather.windStrength));
    }
    const ZoneDiplomacy& diplomacy = contents.diplomacy;
    if (contents.appliesRelations) {
        Diplomacy* table = &server->diplomacy;
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 32; ++j) table->SetRelation(i, j, static_cast<DiplomacyRelation>(diplomacy.relations[i][j]));
    }

    // Every record becomes a world object (the records are freed with `contents`).
    MobRecords& records = contents.records;
    const int step = static_cast<int>(records.byId.size()) / 5 + 1;
    int done = 0;
    for (auto& node : records.byId) {
        Spec* spec = node.second;
        if (!ClassFits(*spec)) {
            game::Warning("map objects: %s record %d: class %d does not fit its kind; the record is left out",
                          name, spec->id, static_cast<const ObjectSpec*>(spec)->parentId);
            ++done;
            continue;
        }
        if (spec->kind == SpecKind::Trap) {
            auto* record = static_cast<TrapSpec*>(spec);
            record->trap.sides = 0;
            record->trap.position = spec->position;
            const std::int32_t* row = diplomacy.relations[record->side];
            const auto hostile = static_cast<std::int32_t>(DiplomacyRelation::Hostile);
            for (int c = 0; c < 32; ++c) record->trap.sides |= static_cast<std::uint32_t>(row[c] == hostile) << c;
        }
        server->CreateObject(spec);
        if (spec->kind == SpecKind::Unit) {
            auto* unitSpec = static_cast<UnitSpec*>(spec);
            Unit* unit = UnitOrNull(unitSpec->object);
            if (unit && (spec->id != 2 || str::Compare(unitSpec->texture, "unmoco2") != 0))
                unit->ServerController()->brain.SetAlarmRecords(unitSpec->behaviour, kBehaviourCount);
        }
        ++done;
        if (done % step == 0) ShowLoadingMovie(done / step + 1);
    }

    // A .lua runs in place of the scripts (Script::LoadLua; docs/notes/mods.md, "Scripts in Lua").
    Script* script = TheMissionScript();
    if (!(keepIds & 0xFF) && !(contents.luaName.empty() ? script->Load(&contents.scripts, 1)
                                                        : script->LoadLua(&contents.scripts, contents.lua, contents.luaName))) {
        // The game goes on without the file's scripts (the original said nothing); the console names the error.
        ScriptErrorInfo error;
        TheMissionScript()->GetError(&error);
        console::EndLine(console::g_consoleOut << "Script error: " << std::string(name) << " line " << error.line
                                               << ": " << error.text);
    }
}

} // namespace mapspec

// --- Mob script decoder ------------------------------------------------------------------------------

// The .mob script text decoder. See mob_script_decoder.h.

std::string MobScriptDecoder::Decode() {
    const char zero = 0;
    const char* coded = input.data();
    const auto length = static_cast<unsigned>(input.size()) - 4;
    text.resize(length, zero);
    std::memcpy(text.data(), coded + 4, length);
    std::uint32_t x = lib::Load<std::uint32_t>(coded);
    for (auto& p : text) {
        x = x * 0x343FDu + 0x269EC3u;
        p = static_cast<char>(p ^ static_cast<char>(x >> 16));
    }
    const int size = static_cast<int>(text.size());
    std::string out;
    std::memmove(str::SetLength(out, size), text.data(), static_cast<std::size_t>(size));
    str::CutAtNull(out);
    return out;
}
