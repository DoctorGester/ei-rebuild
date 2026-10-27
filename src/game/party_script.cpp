// The party a script names (see party_records.h). Names are provisional.
#include "console.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"

#include <string>

namespace {

// The party manager's record list.
const NamedPartyRecords* RecordList() { return &ThePartyManager()->records; }

} // namespace

CParty* __fastcall PartyForScript(CPlayer* player, mfc::CString* name) {
    if (!player) return nullptr;
    std::string owner;
    int split = name->Find("::");
    if (split >= 0) {
        owner = console::Ops(*name).Mid(0, split);
        *name = console::Ops(*name).Mid(split + 2, name->GetLength() - split - 2);
    }
    if (gamecrt::mbscmp(owner.c_str(), RecordList()->text10.c_str()) == 0) return player->Party();
    return (RecordList())->Find(owner.c_str());
}
EI_REWRITTEN(0x004BAEC0, PartyForScript);

