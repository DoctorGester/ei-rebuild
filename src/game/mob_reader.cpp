// The .mob zone file reader (A-S4): 00481F00 reads the top-level chunks, 00481660 turns the
// records into world objects. Names are provisional; the record classes are map_spec.h.
//
// 00481660 has a branch under the global 0079BCF4 (a colour quantisation of the objects' tints,
// 0049BEA0, through STLport map / list / introsort instantiations). Nothing in game.exe,
// addon.dll or EI_Plugin.asi writes that global (it sits in zero-initialised data; a byte search
// finds its address only in the test at 00481704), so the branch never runs and is not
// reproduced: see docs/notes/script.md.
#include "map_spec.h"
#include "options.h"

#include "bink_video.h"
#include "combat.h"
#include "object_diplomacy.h"
#include "script.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <new>

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

constexpr int kKindUnit = 5;
constexpr int kKindTrap = 7;
constexpr int kRelationHostile = 2;
constexpr int kBehaviourCount = 5;

// The limit the .mob wind strength is clamped to (never written: 0).
float MobWindLimit() { return options::TheOptions()->defaultWindForce_; }
// The mission script (CScript at 007C27D4).

// A record of the given chunk id, allocated and constructed, or null for other ids.
Spec* NewRecord(std::uint32_t id) {
    switch (id) {
    case 0xB001:
        if (auto* p = gamecrt::operator_new_as<ObjectSpec>())
            return &p->ObjectSpecConstruct()->base;
        return nullptr;
    case 0xAA01:
        if (auto* p = gamecrt::operator_new_as<LightSpec>())
            return &p->LightSpecConstruct()->effect.base;
        return nullptr;
    case 0xCC01:
        if (auto* p = gamecrt::operator_new_as<SoundSpec>())
            return &p->SoundSpecConstruct()->effect.base;
        return nullptr;
    case 0xDD01:
        if (auto* p = gamecrt::operator_new_as<ParticleSpec>())
            return &p->ParticleSpecConstruct()->effect.base;
        return nullptr;
    case 0xBBAB0000:
        if (auto* p = gamecrt::operator_new_as<TrapSpec>())
            return &p->TrapSpecConstruct()->object.base;
        return nullptr;
    case 0xBBAC0000:
        if (auto* p = gamecrt::operator_new_as<LeverSpec>())
            return &p->LeverSpecConstruct()->object.base;
        return nullptr;
    case 0xBBBB0000:
        if (auto* p = gamecrt::operator_new_as<UnitSpec>())
            return &p->UnitSpecConstruct()->object.base;
        return nullptr;
    case 0xBBBF0000:
        if (auto* p = gamecrt::operator_new_as<FlameSpec>())
            return &p->FlameSpecConstruct()->object.base;
        return nullptr;
    default:
        return nullptr;
    }
}

// 0xB000: every record chunk inside `parent` is read into a new record, keyed by its id.
void ReadRecords(Chunk* parent, MobRecords* records) {
    Chunk chunk(parent);
    do {
        chunk.ReadHeader();
        if (Spec* spec = NewRecord(chunk.id_)) {
            spec->chunk.parent_ = &chunk;
            spec->chunk.ForEachChild();
            if (records->maxId < spec->id) records->maxId = spec->id;
            records->byId[spec->id] = spec;
        }
    } while (parent->size_ != parent->consumed_);
}

// 0xABD0: wind direction and strength and the time of day (two more values are read and unused).
void ReadWeather(Chunk* parent) {
    Chunk chunk(parent);
    Vector3 direction = {0.0f, 1.0f, 0.0f};
    chunk.ReadHeader();
    Vector3 read;
    chunk.ReadVector3(&read);
    direction = read;
    chunk.ReadHeader();
    float strength = chunk.ReadFloat();
    chunk.ReadHeader();
    float time = chunk.ReadFloat();
    chunk.ReadHeader();
    chunk.ReadFloat();
    chunk.ReadHeader();
    chunk.ReadFloat();
    CWorldServer* server = TheServerWorld();
    server->CWorldServer::SetTimeImpl(time);
    float limit = MobWindLimit();
    server->ChangeWind(&direction, limit >= strength ? strength : limit);
}

// 0xDDDDDDD1: the relation table (0x1000 bytes) and the player names.
void ReadPlayers(Chunk* parent, ZoneDiplomacy* diplomacy) {
    Chunk chunk(parent);
    chunk.ReadHeader();
    chunk.ReadBytes(diplomacy->relations, sizeof(diplomacy->relations));
    chunk.ReadHeader();
    diplomacy->names.SetSize(0, -1);
    chunk.ReadStringArray(&diplomacy->names);
}

// The scripts' text, plain or encoded.
void ReadScripts(Chunk* chunk, mfc::CString* scripts, bool encoded) {
    mfc::CString text(mfc::CString::kUnset);   // the callee constructs it
    if (encoded) chunk->ReadEncodedString(&text);
    else chunk->ReadString(&text);
    *scripts = text;
}

} // namespace

bool MobFile::ReadChunks(MobRecords* records, ZoneDiplomacy* diplomacy, mfc::CString* scripts) {
    bool flag = true;
    ReadHeader();
    Chunk child(this);
    do {
        child.ReadHeader();
        const std::uint32_t id = child.id_;
        if (child.size_ != child.consumed_) {
            switch (id) {
            case kChunkScripts: ReadScripts(&child, scripts, false); break;
            case kChunkEncodedScripts: ReadScripts(&child, scripts, true); break;
            case kChunkWeather: ReadWeather(&child); break;
            case kChunkRecords: ReadRecords(&child, records); break;
            case kChunkPlayers: ReadPlayers(&child, diplomacy); break;
            default: break;
            }
        }
        if (id == kChunkFlagOn) flag = true;
        else if (id == kChunkFlagOff) flag = false;
        child.SkipContent();
    } while (size_ != consumed_);
    return flag;
}
EI_REWRITTEN(0x00481F00, MobFile::ReadChunks);

bool MobFile::Load(void*, int keepIds) {
    MobRecords records;
    records.byId.InitBuckets(100);
    records.maxId = 0;
    lib::InPlace<ZoneDiplomacy> diplomacyStorage;
    auto* diplomacy = diplomacyStorage.Get();
    DiplomacyConstruct(diplomacy);
    alignas(mfc::CString) std::uint8_t scriptsStorage[sizeof(mfc::CString)];   // destroyed by hand, in order
    auto* scripts = new (scriptsStorage) mfc::CString();

    if (ReadChunks(&records, diplomacy, scripts)) {
        CDiplomacy* table = GameDiplomacy();
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 32; ++j) table->SetRelation(i, j, diplomacy->relations[i][j], 1);
    }

    // Every record becomes a world object, then is deleted (the map keeps the dangling pointer).
    const int step = static_cast<int>(records.byId.Size()) / 5 + 1;
    int done = 0;
    for (auto* node = records.byId.First(); node; node = records.byId.After(node)) {
        Spec* spec = node->value;
        if (spec->kind == kKindTrap) {
            auto* record = spec->As<TrapSpec>();
            record->trap.sides = 0;
            record->trap.position = spec->position;
            const std::int32_t* row = diplomacy->relations[record->object.side];
            for (int c = 0; c < 32; ++c)
                record->trap.sides |= static_cast<std::uint32_t>(row[c] == kRelationHostile) << c;
        }
        WorldMakeObjectOriginal(TheServerWorld(), nullptr, spec);
        auto* object = spec->As<ObjectSpec>();
        bool skip = spec->id == 2 && GameMbscmp(object->texture, "unmoco2") == 0;
        if (!skip && spec->kind == kKindUnit && spec->object) {
            auto* controller = object->Unit()->ServerController();
            controller->brain.SetAlarmRecords(spec->As<UnitSpec>()->behaviour, kBehaviourCount);
        }
        spec->Record()->DeleteRecord(1);
        ++done;
        if (done % step == 0) ShowLoadingMovie(done / step + 1);
    }

    if (!(keepIds & 0xFF)) TheMissionScript()->Load(scripts, 1);
    scripts->~CString();
    diplomacy->names.Destruct();
    records.byId.Destroy();
    return true;
}
EI_REWRITTEN(0x00481660, MobFile::Load);

} // namespace mapspec
