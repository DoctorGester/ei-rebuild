// The "netstate" step in the image (image_side.h; harness_netstate.cpp).
#include "image_side.h"

#include "../harness_netstate_read.h"

#include "game/net/net_session.h"
#include "game/players/player.h"
#include "game/players/player_client.h"
#include "game/players/saved_parties.h"
#include "game/ui/ui_companions.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include <string>

namespace ei::verify::image {

std::vector<std::string> NetState() { return pair::NetLines<net::ServerEntry>(net::g_gameServer.Get(), net::g_gameClient.Get()); }

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

bool CompanionsOffered() { return ::CompanionsOffered(); }

bool AskCompanion(const char* name) {
    PlayerClient* client = g_clientWorld.LocalView();
    if (!client) return false;
    if (!name) {
        const int unitId = CompanionUnit(0);
        if (unitId < 0) return false;
        client->SendRemoveCompanion(unitId);
        return true;
    }
    for (MpHeroRecord& entry : g_savedParties.entries) {
        const std::string& script = entry.party.members.empty() ? std::string() : entry.party.members[0].scriptName;
        if (script.substr(0, script.find('|')) != name) continue;
        client->SendAddCompanion(entry.GetParty(), false);
        return true;
    }
    return false;
}

bool SavedHero(const char* name, std::string* file, float* experience) {
    file->clear();
    for (MpHeroRecord& entry : g_savedParties.entries) {
        const std::string& script = entry.party.members.empty() ? std::string() : entry.party.members[0].scriptName;
        if (script.substr(0, script.find('|')) == name) *file = entry.file;
    }
    if (file->empty()) return false;
    MpHeroRecord hero;
    hero.file = *file;
    if (!hero.ReadFile(g_savedParties.folder) || hero.party.members.empty()) return false;
    *experience = hero.party.members[0].stats.experience + hero.party.members[0].stats.pendingExperience;
    return true;
}

} // namespace ei::verify::image
