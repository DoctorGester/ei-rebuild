// WorldServer: zone loading and teardown (zone exits, .mob object files, clearing the map objects), the
// script's AddUnitToServer, and writing the map objects into a save. See world.h and docs/notes/world.md.
#include "game/app/app.h"
#include "game/app/movies.h"   // g_loadingMovieOff
#include "game/map/chunk_stream.h"
#include "game/core/core_refcount.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/map/map_spec.h"
#include "game/map/mob_toml.h"
#include "game/net/net_delta.h"
#include "game/map/map_database.h"
#include "game/players/party.h"
#include "game/spells/spell.h"         // SpellEffect: the missiles
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/mods/maps_folder.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"


#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
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

void WorldServer::SetZoneExits(const std::vector<ZoneExit>& source) {
    exits = {};
    for (const ZoneExit& from : source) {
        if (from.target.empty()) continue;   // leads nowhere
        if (from.entrance < 0 && str::Compare(from.target, kNoExit) != 0) {   // a map's data error: left out
            game::Warning("Wrong entrance number %d in zone exits data (to \"%s\"): the exit is left out", from.entrance,
                          from.target);
            continue;
        }
        exits.push_back(from);
    }
}

// ---- Units ------------------------------------------------------------------------------------

void WorldServer::AddUnitToServer(unsigned newId, std::string_view prototype, std::string_view name, const Vector3& position) {
    Party party;   // one member
    PartyUnitRecord* record = party.AddMember("", prototype, name);
    if (!record) return;   // the prototype is unknown (a warning): no unit, the script goes on
    std::vector<PartyUnitRecord*> records{record};
    record->unitId = newId;
    CreateUnits(nullptr, &records, position, kNewUnitRotation, nullptr);
}

void WorldServer::TakeLookFromUnit(PartyUnitRecord* record) {
    if (!record) return;   // AddMember refused an unknown prototype
    Unit* unit = FindUnit(static_cast<int>(record->unitId));
    MapObjectServer* server = unit ? unit->ServerCompanion() : nullptr;
    if (!server) return;
    record->parentIname = server->parentIname;
    record->skinTexture = server->skinTexture;
}

// ---- .mob object files ------------------------------------------------------------------------

void WorldServer::LoadMapObjects(std::string_view fileName, int keepIds) {
    const std::string file(fileName);
    const std::string path = MapPath(file);
    // The .mob's TOML stands for its chunks but the scripts, which stay in the .mob (docs/notes/mods.md, "Map objects
    // as TOML"): with a TOML the .mob may be missing.
    std::optional<mapspec::MobContents> contents = mobtoml::Find(path);
    mapspec::MobFile root;
    const bool opened = root.Open(path, platform::FileMode::Read);
    if (!opened && !contents) game::FatalExit("CWorldServer::LoadMapObjects()\nUnable to read file \"%s\"\n", file);
    if (!contents) contents = root.ReadChunks();
    else if (opened) contents->scripts = root.ReadChunks(false).scripts;
    root.file.Close();
    // The scripts' .lua, which runs in their place (docs/notes/mods.md, "Scripts in Lua").
    if (!(keepIds & 0xFF)) {
        if (std::optional<mobtoml::ScriptLuaFile> lua = mobtoml::FindScriptLua(path, contents->scriptFile)) {
            contents->lua = std::move(lua->text);
            contents->luaName = mobtoml::ScriptLuaName(file, lua->name);
        }
    }
    mapspec::LoadMob(*contents, keepIds, fileName);
}

int WorldServer::LoadMobFile(std::string_view file) {
    for (const std::string& mobFile : mobFiles)
        if (str::Compare(mobFile, file) == 0) return 1;
    g_loadingMovieOff = 1;
    LoadMapObjects(file, 0);
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
        MapObject* object = node.second;
        if (!object) break;
        int objectId = object->id;
        out->WriteValue(objectId);
        Unit* unit = UnitOrNull(object);
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
            // Rebirth's FixProgressNames: gipat2 and jigran get their own movies.
            if (SameName(allod, "gipat2")) app::g_loadingMovieName = "progres2.bik";
            else if (SameName(allod, "jigran")) app::g_loadingMovieName = kProgres5;
        }
    }
    if (zone) {
        if (text::CompareIgnoringCase(*zone, "gz1g") == 0) app::g_loadingMovieName = "progres1.bik";
        if (text::CompareIgnoringCase(*zone, "bz7g") == 0) app::g_loadingMovieName = "progres1.bik";
    }
}
