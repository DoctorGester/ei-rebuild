// The party a script names (see party_records.h). Names are provisional.
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <string>

namespace {

// The party manager's record list.
const NamedPartyRecords* RecordList() { return &ThePartyManager()->records; }

} // namespace

CParty* __fastcall PartyForScript(CPlayer* player, std::string* name) {
    if (!player) return nullptr;
    std::string owner;
    int split = str::Find(*name, "::");
    if (split >= 0) {
        owner = str::Mid(*name, 0, split);
        *name = str::Mid(*name, split + 2, static_cast<int>(name->size()) - split - 2);
    }
    if (gamecrt::mbscmp(owner, RecordList()->text10) == 0) return player->Party();
    return (RecordList())->Find(owner.c_str());
}
EI_REWRITTEN(0x004BAEC0, PartyForScript);

