// The party a script names (A-S4, 004BAEC0; see party_records.h). Names are provisional.
#include "console.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// The party manager's record list (007AA5A0 + 0x24).
const NamedPartyRecords* RecordList() { return &ThePartyManager()->records; }

} // namespace

CParty* __fastcall PartyForScript(void* playerArg, mfc::CString* name) {
    auto* player = static_cast<CPlayer*>(playerArg);
    if (!player) return nullptr;
    mfc::CString owner;
    int split = name->Find("::");
    if (split >= 0) {
        owner = console::Ops(*name).Mid(0, split);
        *name = console::Ops(*name).Mid(split + 2, console::Length(*name) - split - 2);
    }
    if (GameMbscmp(owner, RecordList()->text10) == 0) return player->Party();
    return (RecordList())->Find(owner);
}
EI_REPLACE(0x004BAEC0, PartyForScript);

