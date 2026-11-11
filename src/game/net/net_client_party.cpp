// The client's trade offer (called by the party screens after a change): one member
// message per party member, kept in our PartyMessage. See net_messages.h and docs/notes/network.md.
#include "engine/core/core_refcount.h"

#include "engine/core/core_streams.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/spells/spell.h"
#include "game/ui/ui.h"
#include "game/world/world.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <vector>

namespace net {

namespace {

// The first element in [first, last) whose member stream holds the same bytes as
// `message`'s (sizes, then a byte compare).
using MemberList = std::vector<HeldRef<PartyMemberMessage>>;
MemberList::iterator FindSameMessage(MemberList::iterator first, MemberList::iterator last, PartyMemberMessage* message) {
    ByteStream* stream = &message->stream;
    for (; first != last; ++first) {
        MemoryStream* element = &first->Get()->stream;
        std::uint32_t size = element->Size();
        if (stream->Size() != size) continue;
        const void* a = element->data.data();
        const void* b = message->stream.data.data();
        if (std::memcmp(a, b, stream->Size()) == 0) break;
    }
    return first;
}

// Player 0 of the world (the local player), or null.
Player* LocalPlayer() { return g_serverWorld.GetPlayer(0); }

constexpr const char* kOtherDatabaseOffer =
    "The other player's offer holds items or spells this game's item and spell databases do not have: refused.";

} // namespace

// Rebuilds the member messages of our offer from our party: one per
// item of the party. Each gives its spell's id, type and packed id and the spell's Save,
// or -1 without a spell, then the item's type and packed id and its own Save. A message equal to
// one of the previous messages is replaced by that one (so unchanged members keep their object).
// The offer takes the party's money; then its counters move on and its accepted flag is cleared.
void ClientSession::PublishParty() {
    if (!party) return;
    Party* own = &parties[0];
    // A copy of the old messages, then the list emptied.
    MemberList previous = party->members;
    party->members.clear();
    party->money = static_cast<std::uint32_t>(own->money);

    for (Item* item : own->items) {
        auto* message = new PartyMemberMessage();
        HeldRef<PartyMemberMessage> ref(message);
        ByteStream* stream = &message->stream;
        stream->Clear();
        Spell* spell = item->spell;
        stream->WriteId(spell);
        if (spell) {
            stream->WriteValue(spell->type);
            stream->WriteValue(spell->prototypeId);
            spell->Save(stream);
        }
        stream->WriteValue(item->type);
        stream->WriteValue(item->prototypeId);
        item->Save(stream);

        auto same = FindSameMessage(previous.begin(), previous.end(), message);
        if (same != previous.end()) {
            party->members.push_back(*same);
            previous.erase(same);
        } else {
            party->members.push_back(ref);
        }
    }

    ++party->revision;
    party->accepted = 0;
    ++party->version;
}

// The received offer that pairs with ours: its partner is our
// player and its player our partner, and it is live. Null without one.
PartyMessage* ClientSession::FindParty() {
    if (!party) return nullptr;
    for (auto& node : offers) {
        auto* other = node.Get();
        if (other->partner.Get() != party->player.Get() || other->player.Get() != party->partner.Get()) continue;
        if (IsLive(other)) return other;
    }
    return nullptr;
}

// An accepted offer is unaccepted with its version moved on, so it is sent again.
void ClientSession::CommitParty() {
    if (!party || !party->accepted) return;
    party->accepted = 0;
    ++party->version;
}

// Drops the party description (the owner's release).
void ClientSession::DropParty() { DestroyOwned(party); }

// Rebuilds the partner's party from the paired offer, once every member message of it holds data. Each
// message gives (when its first dword, the spell's id, is positive) a spell made by the party's spell manager
// (CreateObject with the stored type and that id, its packed id set and its Load) and an
// item made by the item manager (CreateObject with id -1, packed id, Load). The items are added to
// the party, which takes the offer's money first (an offer naming records this game lacks leaves the party
// empty); trade records older than the offer's revision are dropped.
void ClientSession::ApplyParty() {
    PartyMessage* remote = FindParty();
    if (!remote) return;
    for (const auto& member : remote->members) {
        if (!IsLive(member.Get())) return;
        if (member.Get()->stream.Size() == 0) return;
    }

    Party* partner = &parties[1];
    partner->Clear(true, false);
    partner->money = static_cast<int>(remote->money);
    std::vector<Item*> items;   // each holds a reference
    for (const auto& member : remote->members) {
        ByteStream* stream = &member->stream;
        stream->Seek(0);
        const int id = stream->ReadId();
        std::uint32_t type, packedId;
        if (id > 0) {
            stream->ReadValue(type);
            stream->ReadValue(packedId);
            EngineObject* spell = partner->GetSpellManager()->CreateObject(static_cast<int>(type), id);
            if (!spell) break;   // a type the factory does not know: the rest of the offer cannot be read
            spell->prototypeId = packedId;
            spell->Load(stream);
        }
        stream->ReadValue(type);
        stream->ReadValue(packedId);
        auto* item = partner->ItemFactory()->CreateObjectAs<Item>(static_cast<int>(type), -1);
        if (!item) break;
        item->prototypeId = packedId;
        item->Load(stream);
        item->AddRef();
        items.push_back(item);
    }
    if (HasEveryRecord(partner)) {
        for (Item* item : items) partner->AddItem(item, true, true);
    } else {
        // An offer naming items or spells this game's databases lack (a partner with another items.idb or
        // spells.sdb) is refused: the partner's party stays empty and the chat says why.
        for (Item* item : items) item->Release();
        items.clear();
        partner->Clear(true, false);
        g_interface.OnChatLine(kOtherDatabaseOffer);
    }

    std::int32_t revision = static_cast<std::int32_t>(remote->revision);
    for (auto record = tradeRecords.begin(); record != tradeRecords.end();)
        record = static_cast<std::int32_t>(record->acceptedRevision) < revision ? tradeRecords.erase(record) : std::next(record);
    for (EngineObject* item : items)
        if (item) item->Release();
}

// --- Net client awards -------------------------------------------------------------------------------

// The game client's tick and its award handling: timers count down,
// and received awards are applied to the local player. See net/net_session.h and docs/notes/network.md.

// Applies the received awards (net_server_trade.cpp) to player 0 and empties the list. Experience; Gold:
// money (the party's, the value truncated), then an empty item list given; Item: the item named by the text, given; RemoveItem:
// the first given item whose IsSameAs with the named item returns 0 is taken (by id); QuestReset: the party's quest items
// removed and every given quest item (class 0x14) taken.
void ClientSession::ApplyAwards() {
    Player* localPlayer = LocalPlayer();
    if (!localPlayer) return;
    for (auto& node : awards) {
        auto* award = node.Get();
        const char* name = award->text.data();
        switch (award->kind) {
        case AwardKind::Experience: {
            g_serverWorld.AwardExperience(nullptr, award->value);
            break;
        }
        case AwardKind::Gold: {
            std::vector<Item*> none;
            // Truncated as _ftol does: through 64 bits, so a NaN or a value beyond the int range gives 0.
            localPlayer->GetParty()->money += static_cast<int>(static_cast<std::int64_t>(award->value));
            localPlayer->GiveItems(none, 0);
            break;
        }
        case AwardKind::Item: {
            std::vector<Item*> items;
            Item* item = localPlayer->GetParty()->ItemFactory()->CreateItem(name);
            if (item) item->AddRef();
            items.push_back(item);
            localPlayer->GiveItems(items, 0);
            ReleaseAndClear(items);
            break;
        }
        case AwardKind::RemoveItem: {
            auto* item = localPlayer->GetParty()->ItemFactory()->CreateItem(name);
            if (item) item->AddRef();
            for (Item* owned : localPlayer->items) {
                if (!owned->IsSameAs(item)) {
                    std::int32_t id = owned->id;
                    if (id >= 0) localPlayer->TakeItem(id);   // (the walk ends here)
                    break;
                }
            }
            if (item) item->Release();
            break;
        }
        case AwardKind::QuestReset: {
            localPlayer->GetParty()->RemoveQuestItems();
            // TakeItem erases from the list: the quest items' ids first.
            std::vector<int> quest;
            for (Item* owned : localPlayer->items)
                if (owned->GetClassId() == ClassId::ItemQuest) quest.push_back(owned->id);
            for (int id : quest) localPlayer->TakeItem(id);
            break;
        }
        default:
            break;
        }
    }

    awards.clear();
}

// The client base tick, then the timers count down by `dt` (as a float) and those below zero are dropped. Awards
// are applied in a game of mode 3 when player 0 exists with its connection in state 1 and the game-flow flag is set.
void ClientSession::Tick(double dt) {
    ClientBase::Tick(dt);
    CountDown(timers, static_cast<float>(dt));
    if (awards.empty() || g_serverWorld.zoneMode != ZoneMode::Base) return;
    Player* localPlayer = LocalPlayer();
    if (!localPlayer || localPlayer->Server()->state != PlayerServer::State::Joined || !g_serverWorld.zoneLoaded) return;
    ApplyAwards();
}

} // namespace net
