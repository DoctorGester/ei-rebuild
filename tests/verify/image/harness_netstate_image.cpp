// The "netstate" step in the image (image_side.h; harness_netstate.cpp).
#include "image_side.h"

#include "../harness_netstate_read.h"

#include "game/items/item.h"
#include "game/net/mod_identity.h"
#include "game/net/net_session.h"
#include "game/players/player.h"
#include "game/players/player_client.h"
#include "game/players/saved_parties.h"
#include "game/script/script.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include <string>

namespace ei::verify::image {

std::vector<std::string> NetState() { return pair::NetLines<net::ServerEntry>(net::g_gameServer.Get(), net::g_gameClient.Get()); }

namespace {

// A mod as the netmod lines name it: its title and whether its hash is this game's (the hash itself depends on the
// checkout's line ends), "no mod" or "no mark".
std::string ShownMod(const std::optional<modidentity::Identity>& mod) {
    if (!mod) return "no mark";
    if (mod->title.empty()) return "no mod";
    const bool same = mod->hash == modidentity::Played().hash;
    return "\"" + mod->title + "\" (" + (same ? "this game's files" : "other files") + ")";
}

} // namespace

std::vector<std::string> NetMod() {
    std::vector<std::string> lines;
    const modidentity::Identity& played = modidentity::Played();
    lines.push_back("netmod: this game plays " + (played.title.empty() ? std::string("no mod") : "\"" + played.title + "\""));
    if (const net::ClientSession* client = net::g_gameClient.Get()) {
        lines.push_back("netmod: the client's join state " + std::to_string(static_cast<unsigned>(client->joinState)) +
                        ", refusal " + std::to_string(static_cast<unsigned>(client->refusal)) + ", the host's mod " +
                        ShownMod(client->hostMod));
    }
    if (const net::ServerSession* server = net::g_gameServer.Get()) {
        std::string line = "netmod: the server refused " + std::to_string(server->modRefusals) + " joins for their mod";
        if (server->modRefusals) line += ", the last playing " + ShownMod(server->modRefused);
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> NetList() {
    std::vector<std::string> lines;
    const net::ClientSession* client = net::g_gameClient.Get();
    if (!client) return {"netlist: no client session"};
    for (const auto& listed : client->servers) {
        const net::ServerEntry* server = listed.Get();
        lines.push_back("netlist: \"" + server->name + "\" plays " + (server->modRead ? ShownMod(server->mod) : "(no answer read)") +
                        (server->ModAgrees() ? ", joinable" : ", cannot join for the mods"));
    }
    if (lines.empty()) lines.push_back("netlist: no servers");
    return lines;
}

std::string ServerScript(const std::string& line) {
    ScriptErrorInfo error{};
    if (!g_serverWorld.RunCommandScript("WorldScript(" + line + ")", true, &error))
        return "serverscript " + line + ": " + error.text;
    return {};
}

void OriginalNet() { net::g_originalPeer = true; }

int ServerAwards() {
    const net::ServerSession* server = net::g_gameServer.Get();
    return server ? static_cast<int>(server->awards.size()) : -1;
}

int SessionPlayerLimit() {
    const net::ClientSession* client = net::g_gameClient.Get();
    return client ? client->PlayerLimit() : -1;
}

int ServerPartyRecords(int playerId) {
    Player* player = g_serverWorld.GetPlayer(playerId);
    return player ? static_cast<int>(player->GetParty()->members.size()) : -1;
}

int ServerPartyUnits(int playerId) {
    Player* player = g_serverWorld.GetPlayer(playerId);
    if (!player) return -1;
    int count = 0;
    for (auto node = player->units.begin(); node != player->units.end() && node->second; ++node) ++count;
    return count;
}

namespace {

template <typename List>
int CountOfType(const List& items, unsigned type) {
    int count = 0;
    for (const Item* item : items)
        if (item && static_cast<unsigned>(item->type) == type) ++count;
    return count;
}

// The saved hero of the heroes' list named `name` (the script name up to the '|'), its file read from the disk into
// `hero`; false without one or when it does not read.
bool ReadSavedHero(const char* name, MpHeroRecord* hero) {
    for (MpHeroRecord& entry : g_savedParties.entries) {
        const std::string& script = entry.party.members.empty() ? std::string() : entry.party.members[0].scriptName;
        if (script.substr(0, script.find('|')) == name) hero->file = entry.file;
    }
    return !hero->file.empty() && hero->ReadFile(g_savedParties.folder) && !hero->party.members.empty();
}

} // namespace

int CompanionItems(int index, unsigned type) {
    Unit* unit = g_serverWorld.FindUnit(CompanionUnit(index));
    if (!unit) return -1;
    return CountOfType(unit->weapons, type) + CountOfType(unit->armor, type) + CountOfType(unit->quickItems, type) +
           CountOfType(unit->spells, type);
}

int SavedHeroItems(const char* name, unsigned type) {
    MpHeroRecord hero;
    if (!ReadSavedHero(name, &hero)) return -1;
    const PartyUnitRecord& record = hero.party.members[0];
    return CountOfType(hero.party.items, type) + CountOfType(record.weapons, type) + CountOfType(record.armor, type) +
           CountOfType(record.quickItems, type) + CountOfType(record.spells, type);
}

int CompanionUnit(int index) {
    Player* player = g_serverWorld.GetPlayer(0);
    if (!player) return -1;
    const auto& members = player->GetParty()->members;
    return index + 1 < std::ssize(members) ? static_cast<int>(members[index + 1].unitId) : -1;
}

int NearestEnemy(int id) {
    Unit* from = g_serverWorld.FindUnit(id);
    if (!from) return -1;
    int nearest = -1;
    float best = 0.0f;
    for (auto node = g_serverWorld.objects.map.begin(); node != g_serverWorld.objects.map.end() && node->second; ++node) {
        Unit* unit = g_serverWorld.FindUnit(node->second->id);
        if (!unit || unit->IsDead() || !from->IsHostileTo(unit)) continue;
        const float distance = LengthSquared(unit->localPosition - from->localPosition);
        if (nearest < 0 || distance < best) {
            nearest = unit->id;
            best = distance;
        }
    }
    return nearest;
}

int CompanionList() { return static_cast<int>(g_savedParties.companions.size()); }

bool AskCompanion(const char* name) {
    PlayerClient* client = g_clientWorld.LocalView();
    if (!client) return false;
    for (MpHeroRecord& entry : g_savedParties.entries) {
        const std::string& script = entry.party.members.empty() ? std::string() : entry.party.members[0].scriptName;
        if (script.substr(0, script.find('|')) != name) continue;
        client->SendAddCompanion(entry.GetParty(), false);
        return true;
    }
    return false;
}

bool SavedHero(const char* name, std::string* file, float* experience) {
    MpHeroRecord hero;
    const bool read = ReadSavedHero(name, &hero);
    *file = hero.file;
    if (!read) return false;
    *experience = hero.party.members[0].stats.experience + hero.party.members[0].stats.pendingExperience;
    return true;
}

} // namespace ei::verify::image
