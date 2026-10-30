// The party a script names (see party_records.h). Names are provisional.
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"

#include "game_singletons.h"
#include "lib/str.h"

#include <string>

namespace {

// The party manager's record list.
const NamedPartyRecords* RecordList() { return &g_partyManager.records; }

} // namespace

CParty* PartyForScript(CPlayer* player, std::string* name) {
    if (!player) return nullptr;
    std::string owner;
    int split = str::Find(*name, "::");
    if (split >= 0) {
        owner = str::Mid(*name, 0, split);
        *name = str::Mid(*name, split + 2, static_cast<int>(name->size()) - split - 2);
    }
    if (str::Compare(owner, RecordList()->currentName) == 0) return player->GetParty();
    return (RecordList())->Find(owner);
}
