// The .mob zone file reader (A-S4): 00481F00 reads the top-level chunks, 00481660 turns the
// records into world objects. Names are provisional; the record classes are map_spec.h.
//
// 00481660 has a branch under the global 0079BCF4 (a colour quantisation of the objects' tints,
// 0049BEA0, through STLport map / list / introsort instantiations). Nothing in game.exe,
// addon.dll or EI_Plugin.asi writes that global (it sits in zero-initialised data; a byte search
// finds its address only in the test at 00481704), so the branch never runs and is not
// reproduced: see docs/notes/script.md.
#include "map_spec.h"

#include "bink_video.h"
#include "combat.h"
#include "object_diplomacy.h"
#include "script.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_world.h"

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
float MobWindLimit() { return *reinterpret_cast<const float*>(0x0079BE58); }
// The mission script (CScript at 007C27D4).
CScript* MissionScript() { return reinterpret_cast<CScript*>(0x007C27D4); }

// 005D6D20 (RET 8): the AI brain takes `count` alarm records (0x4C bytes each) from `records`.
__declspec(naked) void __fastcall BrainSetAlarmRecords(void* /*brain*/, void*, const void* /*records*/, int /*count*/)
    EI_ORIGINAL(0x005D6D20)

// A record of the given chunk id, allocated and constructed, or null for other ids.
Spec* NewRecord(std::uint32_t id) {
    switch (id) {
    case 0xB001:
        if (void* p = gamecrt::operator_new(sizeof(ObjectSpec)))
            return &static_cast<ObjectSpec*>(p)->ObjectSpecConstruct()->base;
        return nullptr;
    case 0xAA01:
        if (void* p = gamecrt::operator_new(0x68))
            return reinterpret_cast<Spec*>(static_cast<LightSpec*>(p)->LightSpecConstruct());
        return nullptr;
    case 0xCC01:
        if (void* p = gamecrt::operator_new(0x88))
            return reinterpret_cast<Spec*>(static_cast<SoundSpec*>(p)->SoundSpecConstruct());
        return nullptr;
    case 0xDD01:
        if (void* p = gamecrt::operator_new(0x5C))
            return reinterpret_cast<Spec*>(static_cast<ParticleSpec*>(p)->ParticleSpecConstruct());
        return nullptr;
    case 0xBBAB0000:
        if (void* p = gamecrt::operator_new(sizeof(TrapSpec)))
            return &static_cast<TrapSpec*>(p)->TrapSpecConstruct()->object.base;
        return nullptr;
    case 0xBBAC0000:
        if (void* p = gamecrt::operator_new(sizeof(LeverSpec)))
            return &static_cast<LeverSpec*>(p)->LeverSpecConstruct()->object.base;
        return nullptr;
    case 0xBBBB0000:
        if (void* p = gamecrt::operator_new(0x324))
            return reinterpret_cast<Spec*>(UnitSpecConstruct(static_cast<UnitSpec*>(p)));
        return nullptr;
    case 0xBBBF0000:
        if (void* p = gamecrt::operator_new(sizeof(FlameSpec)))
            return &static_cast<FlameSpec*>(p)->FlameSpecConstruct()->object.base;
        return nullptr;
    default:
        return nullptr;
    }
}

// 0xB000: every record chunk inside `parent` is read into a new record, keyed by its id.
void ReadRecords(Chunk* parent, MobRecords* records) {
    Chunk chunk;
    chunk.Construct(parent);
    do {
        chunk.ReadHeader();
        if (Spec* spec = NewRecord(chunk.id_)) {
            spec->chunk.parent_ = &chunk;
            spec->chunk.ForEachChild();
            if (records->maxId < spec->id) records->maxId = spec->id;
            records->byId[spec->id] = spec;
        }
    } while (parent->size_ != parent->consumed_);
    chunk.Destruct();
}

// 0xABD0: wind direction and strength and the time of day (two more values are read and unused).
void ReadWeather(Chunk* parent) {
    Chunk chunk;
    chunk.Construct(parent);
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
    CWorldServer* server = WorldServer();
    server->CWorldServer::SetTime(time);
    float limit = MobWindLimit();
    server->ChangeWind(&direction, !(limit >= strength) ? limit : strength);
    chunk.Destruct();
}

// 0xDDDDDDD1: the relation table (0x1000 bytes) and the player names.
void ReadPlayers(Chunk* parent, ZoneDiplomacy* diplomacy) {
    Chunk chunk;
    chunk.Construct(parent);
    chunk.ReadHeader();
    chunk.ReadBytes(diplomacy->relations, sizeof(diplomacy->relations));
    chunk.ReadHeader();
    StringArraySetSize(&diplomacy->names, nullptr, 0, -1);
    chunk.ReadStringArray(&diplomacy->names);
    chunk.Destruct();
}

// The scripts' text, plain or encoded.
void ReadScripts(Chunk* chunk, mfc::CString* scripts, bool encoded) {
    alignas(mfc::CString) std::uint8_t slot[sizeof(mfc::CString)];   // the callee constructs it
    auto* text = reinterpret_cast<mfc::CString*>(slot);
    if (encoded) chunk->ReadEncodedString(text);
    else chunk->ReadString(text);
    *scripts = *text;
    text->~CString();
}

} // namespace

bool __fastcall ReadMobChunks(Chunk* root, void*, MobRecords* records, ZoneDiplomacy* diplomacy,
                              mfc::CString* scripts) {
    bool flag = true;
    root->ReadHeader();
    Chunk child;
    child.Construct(root);
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
    } while (root->size_ != root->consumed_);
    child.Destruct();
    return flag;
}
EI_REPLACE(0x00481F00, ReadMobChunks);

bool __fastcall LoadMobFile(Chunk* root, void*, void*, int keepIds) {
    MobRecords records;
    records.byId.InitBuckets(100);
    records.maxId = 0;
    alignas(ZoneDiplomacy) std::uint8_t diplomacyStorage[sizeof(ZoneDiplomacy)];
    auto* diplomacy = reinterpret_cast<ZoneDiplomacy*>(diplomacyStorage);
    DiplomacyConstruct(diplomacy->relations[0]);
    alignas(mfc::CString) std::uint8_t scriptsStorage[sizeof(mfc::CString)];   // destroyed by hand, in order
    auto* scripts = new (scriptsStorage) mfc::CString();

    if (ReadMobChunks(root, nullptr, &records, diplomacy, scripts)) {
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
            auto* trap = reinterpret_cast<TrapSpec*>(spec);
            trap->diplomacy = 0;
            trap->origin = spec->position;
            const std::int32_t* row = diplomacy->relations[trap->object.unknown_6E];
            for (int c = 0; c < 32; ++c)
                trap->diplomacy |= static_cast<std::uint32_t>(row[c] == kRelationHostile) << c;
        }
        WorldMakeObjectOriginal(WorldServer(), nullptr, spec);
        auto* object = reinterpret_cast<ObjectSpec*>(spec);
        bool skip = spec->id == 2 && GameMbscmp(object->texture, "unmoco2") == 0;
        if (!skip && spec->kind == kKindUnit && spec->object) {
            auto* controller = static_cast<CUnitServer*>(static_cast<CUnit*>(spec->object)->controller);
            BrainSetAlarmRecords(&controller->brain, nullptr, UnitSpecBehaviours(reinterpret_cast<UnitSpec*>(spec)),
                                 kBehaviourCount);
        }
        reinterpret_cast<SpecRecord*>(spec)->DeleteRecord(1);
        ++done;
        if (done % step == 0) ShowLoadingMovie(done / step + 1);
    }

    if (!(keepIds & 0xFF)) MissionScript()->Load(scripts, 1);
    scripts->~CString();
    StringArrayDestruct(&diplomacy->names);
    records.byId.Destroy();
    return true;
}
EI_REPLACE(0x00481660, LoadMobFile);

} // namespace mapspec
