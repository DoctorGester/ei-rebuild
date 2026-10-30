// WorldServer: zone loading and teardown (zone exits, .mob object files, clearing the map objects), the
// script's AddUnitToServer, and writing the map objects into a save. See world.h and docs/notes/world.md.
#include "app.h"
#include "bink_video.h"   // g_loadingMovieOff
#include "chunk_stream.h"
#include "core_refcount.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_spec.h"
#include "model.h"   // TweenArray (countdowns)
#include "net_delta.h"
#include "object_tables.h"
#include "party.h"
#include "spell.h"         // SpellEffect: the missiles
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "world_map_files.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/rebirth.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// ---- Game data --------------------------------------------------------------------------------

// The target of an exit that leads nowhere.
constexpr const char* kNoExit = "none";

// ---- Helpers ----------------------------------------------------------------------------------

bool SameName(std::string_view a, std::string_view b) {
    return str::Compare(a, b) == 0;
}
// "progres5.bik" is not among the game's strings; addon.dll points the name at its own copy.
constexpr const char* kProgres5 = "progres5.bik";

} // namespace

// ---- Zone exits -------------------------------------------------------------------------------

void WorldServer::SetZoneExits(const std::vector<ZoneExit>* source) {
    exits = {};
    for (const ZoneExit& from : *source) {
        if (str::Compare(from.target, game::kEmptyText) == 0) continue;   // leads nowhere
        if (from.entrance < 0 && str::Compare(from.target, kNoExit) != 0)
            game::FatalError(game::zonetexts::WrongEntrance());
        exits.push_back(from);
    }
}

// ---- Units ------------------------------------------------------------------------------------

void WorldServer::AddUnitToServer(unsigned newId, const char* prototype, const char* name, const Vector3& position) {
    CParty party;   // one member
    party.AddMember(game::kEmptyText, prototype, name);
    std::vector<PartyUnitRecord*> records;
    PartyUnitRecord* record = party.members.data();
    records.resize(1);
    records[0] = record;
    record->unitId = newId;
    CreateUnits(nullptr, &records, position, kNewUnitRotation, nullptr);
}

// ---- .mob object files ------------------------------------------------------------------------

int WorldServer::LoadMapObjects(std::string_view fileName, int keepIds) {
    const std::string file(fileName);
    mapspec::MobFile rootChunk;
    mapspec::MobFile* root = &rootChunk;
    auto* fileObject = &root->file;

    std::string path = MapPath(file);
    int result;
    if (!root->Open(path, lib::File::modeRead | lib::File::shareDenyWrite)) {
        result = game::FatalError(game::zonetexts::CantOpenObjects(), file);
    } else {
        bool read = root->Load(keepIds);
        fileObject->Close();
        result = read ? 1 : game::FatalError(game::zonetexts::CantReadObjects(), file);
    }
    return result;   // the file closed with the root
}

int WorldServer::LoadMobFile(std::string_view file) {
    for (const std::string& mobFile : mobFiles)
        if (str::Compare(mobFile, file) == 0) return 1;
    g_loadingMovieOff = 1;
    {
        int loadedCount = LoadMapObjects(file, 0);
        if (loadedCount == 0) {
            g_loadingMovieOff = 0;
            zoneLoaded = 0;
            return game::FatalError(game::zonetexts::CantLoadMob(), std::string(file));
        }
    }
    g_loadingMovieOff = 0;
    mobFiles.push_back(std::string(file));
    return 1;
}

// ---- Clearing and saving the map objects ----------------------------------------------------

void WorldServer::ClearMapObjects() {
    // The objects are collected first, then removed one by one.
    std::vector<EngineObject*> pending;
    for (auto& node : mapObjects.map) {
        EngineObject* object = node.second;
        if (!object) break;   // the walk stops at the first empty entry
        pending.push_back(object);
    }
    for (EngineObject* object : pending) RemoveObject(object->id);

    projectiles.clear();

    ReleaseAndClear(counted);

    countdowns.entries.clear();
}

void WorldServer::SaveMapObjects(ByteStream* out) {
    for (auto& node : mapObjects.map) {
        CMapObject* object = node.second;
        if (!object) break;
        int objectId = object->id;
        out->WriteValue(objectId);
        CUnit* unit = UnitOrNull(object);
        const std::string_view prototype = unit ? std::string_view(unit->GetPrototype()->name) : std::string_view(object->name);
        out->WriteString(prototype);
        object->Save(out);
        // The parts changed and those already sent (the network state's mask).
        std::uint32_t stateFlags = static_cast<std::uint32_t>(object->netState.mask) |
                              static_cast<std::uint32_t>(object->netState.sentMask);
        out->WriteValue(stateFlags);
    }
    int end = -1;
    out->WriteValue(end);
}

// ---- Loading movie -------------------------------------------------------------------

void SelectLoadingMovie(std::optional<std::string_view> zone) {
    std::string allod;
    if (zone) {
        MapRecord* map = g_mapDatabase.FindMap(*zone);
        if (map) allod = map->allod;
    }
    if (SameName(allod, "gipat")) {
        app::g_loadingMovieName = "progres2.bik";
    } else if (SameName(allod, "ingos")) {
        app::g_loadingMovieName = "progres3.bik";
    } else {
        bool suslanger = SameName(allod, "suslanger");
        app::g_loadingMovieName = "progres4.bik";
        if (!suslanger) {
            app::g_loadingMovieName = "progres.bik";
            if (ei::rebirth::Enabled(ei::rebirth::Patch::FixProgressNames)) {
                if (SameName(allod, "gipat2")) app::g_loadingMovieName = "progres2.bik";
                else if (SameName(allod, "jigran")) app::g_loadingMovieName = kProgres5;
            }
        }
    }
    if (zone) {
        if (text::CompareIgnoringCase(*zone, "gz1g") == 0) app::g_loadingMovieName = "progres1.bik";
        if (text::CompareIgnoringCase(*zone, "bz7g") == 0) app::g_loadingMovieName = "progres1.bik";
    }
}
