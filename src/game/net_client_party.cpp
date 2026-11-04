// The client's trade offer (called by the party screens after a change): one member
// message per party member, kept in our PartyMessage. See net_messages.h and docs/notes/network.md.
#include "core_refcount.h"

#include "core_streams.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "net_messages.h"
#include "party.h"
#include "player_messages.h"
#include "spell.h"
#include "world.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <vector>

namespace net {

namespace {

void WriteDword(ByteStream* stream, std::uint32_t* scratch, std::uint32_t value) {
    *scratch = value;
    stream->Write(scratch, 4u);
}

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

} // namespace

// Rebuilds the member messages of our offer from our party: one per
// item of the party. Each gives its spell's id, type and packed id and the spell's Save,
// or -1 without a spell, then the item's type and packed id and its own Save. A message equal to
// one of the previous messages is replaced by that one (so unchanged members keep their object).
// The offer takes the party's money; then its counters move on and its accepted flag is cleared.
void ClientPublishParty(ClientSession* self) {
    Party* party = self->parties[0].Get();
    // Read again at each use.
    auto state = [self] { return self->party.Get(); };
    if (!state()) return;
    // A copy of the old messages, then the list emptied.
    MemberList previous = state()->members;
    state()->members.clear();
    state()->money = static_cast<std::uint32_t>(party->money);

    std::uint32_t scratch = 0;
    for (int i = 0; i < std::ssize(party->items); ++i) {
        auto* message = NewCounted<PartyMemberMessage>();
        HeldRef<PartyMemberMessage> ref(message);
        ByteStream* stream = &message->stream;
        Item* item = party->items[i];
        stream->Clear();
        Spell* spell = item->spell;
        if (spell) {
            WriteDword(stream, &scratch, static_cast<std::uint32_t>(spell->id));
            WriteDword(stream, &scratch, static_cast<std::uint32_t>(spell->type));
            WriteDword(stream, &scratch, spell->prototypeId);
            spell->Save(stream);
        } else {
            WriteDword(stream, &scratch, 0xFFFFFFFFu);
        }
        WriteDword(stream, &scratch, static_cast<std::uint32_t>(item->type));
        WriteDword(stream, &scratch, item->prototypeId);
        item->Save(stream);

        auto same = FindSameMessage(previous.begin(), previous.end(), message);
        PartyMessage* current = state();
        if (same != previous.end()) {
            current->members.push_back(*same);
            previous.erase(same);
        } else {
            current->members.push_back(ref);
        }
    }

    ++state()->revision;
    state()->accepted = 0;
    ++state()->version;
}

// The received offer that pairs with ours: its partner is our
// player and its player our partner, and it is live. Null without one.
PartyMessage* ClientFindParty(ClientSession* self) {
    auto* ours = self->party.Get();
    if (!ours) return nullptr;
    for (auto& node : self->offers) {
        auto* other = node.Get();
        if (other->partner.Get() != ours->player.Get() || other->player.Get() != ours->partner.Get()) continue;
        if (other && !(other->flags7 & RefObject::kDetached)) return other;
    }
    return nullptr;
}

// An accepted offer is unaccepted with its version moved on, so it is sent again.
void ClientCommitParty(ClientSession* self) {
    auto* state = self->party.Get();
    if (!state || !state->accepted) return;
    state->accepted = 0;
    ++self->party.Get()->version;
}

// Rebuilds the partner's party from the paired offer, once every member message of it holds data. Each
// message gives (when its first dword, the spell's id, is positive) a spell made by the party's spell manager
// (CreateObject with the stored type and that id, its packed id set and its Load) and an
// item made by the item manager (CreateObject with id -1, packed id, Load). The items are added to
// the party, which takes the offer's money first; trade records older than the offer's revision are dropped.
void ClientApplyParty(ClientSession* self) {
    PartyMessage* remote = ClientFindParty(self);
    if (!remote) return;
    auto count = [remote] { return static_cast<std::uint32_t>(remote->members.size()); };
    for (std::uint32_t i = 0; i < count(); ++i) {
        PartyMemberMessage* message = remote->members[i].Get();
        if (!message || (message->flags7 & RefObject::kDetached)) return;
        if (message->stream.Size() == 0) return;
    }

    Party* party = self->parties[1].Get();
    party->Clear(1, 0);
    party->money = static_cast<int>(remote->money);
    std::vector<Item*> items;   // each holds a reference
    for (std::uint32_t i = 0; i < count(); ++i) {
        ByteStream* stream = &remote->members[i]->stream;
        stream->Seek(0);
        std::int32_t id;
        std::uint32_t type, packedId;
        stream->ReadValue(id);
        if (id > 0) {
            stream->ReadValue(type);
            stream->ReadValue(packedId);
            EngineObject* spell = party->GetSpellManager()->CreateObject(static_cast<int>(type), id);
            if (!spell) break;   // a type the factory does not know: the rest of the offer cannot be read
            spell->prototypeId = packedId;
            spell->Load(stream);
        }
        stream->ReadValue(type);
        stream->ReadValue(packedId);
        auto* item = party->ItemFactory()->CreateObjectAs<Item>(static_cast<int>(type), -1);
        if (!item) break;
        item->prototypeId = packedId;
        item->Load(stream);
        item->AddRef();
        items.push_back(item);
    }
    for (Item* item : items) party->AddItem(item, 1, 1);

    std::int32_t revision = static_cast<std::int32_t>(remote->revision);
    std::list<TradeRecord>& records = self->tradeRecords;
    for (auto record = records.begin(); record != records.end();)
        record = static_cast<std::int32_t>(record->acceptedRevision) < revision ? records.erase(record) : std::next(record);
    for (EngineObject* item : items)
        if (item) item->Release();
}

// --- Net client awards -------------------------------------------------------------------------------

// The game client's tick and its award handling: timers count down,
// and received awards are applied to the local player. See net_classes.h and docs/notes/network.md.

// Applies the received awards (net_server_trade.cpp) to player 0 and empties the list. Experience; Gold:
// money (the party's, the value truncated), then an empty item list given; Item: the item named by the text, given; RemoveItem:
// the first given item whose IsSameAs with the named item returns 0 is taken (by id); QuestReset: the party's quest items
// removed and every given quest item (class 0x14) taken.
void ClientSession::ApplyAwards() {
    ClientSession* self = this;
    Player* localPlayer = LocalPlayer();
    if (!localPlayer) return;
    for (auto& node : self->awards) {
        auto* award = node.Get();
        const char* name = award->text.data();
        switch (award->kind) {
        case AwardKind::Experience: {
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(award->value);
            g_serverWorld.AwardExperience(nullptr, std::bit_cast<float>(bits));
            break;
        }
        case AwardKind::Gold: {
            std::vector<Item*> none;
            // Truncated as _ftol does: through 64 bits, so a NaN or a value beyond the int range gives 0.
            localPlayer->GetParty()->money += static_cast<int>(static_cast<std::int64_t>(award->value));
            localPlayer->GiveItems(&none, 0);
            break;
        }
        case AwardKind::Item: {
            std::vector<Item*> items;
            Item* item = localPlayer->GetParty()->ItemFactory()->CreateItem(name);
            if (item) item->AddRef();
            items.push_back(item);
            localPlayer->GiveItems(&items, 0);
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

    // The list emptied inline: each award released, each node back to the 16-byte pool list.
    self->awards.clear();
}

// The client base tick, then the timers count down by `dt` (as a float) and those below zero are dropped. Awards
// are applied in a game of mode 3 when player 0 exists with its connection in state 1 and the game-flow flag is set.
void ClientSession::Tick(double dt) {
    ClientSession* self = this;
    self->ClientBase::Tick(dt);
    CountDown(self->timers, static_cast<float>(dt));
    if (self->awards.begin() == self->awards.end() || g_serverWorld.zoneMode != ZoneMode::Base) return;
    Player* localPlayer = LocalPlayer();
    if (!localPlayer || localPlayer->Server()->state != PlayerServer::State::Joined || !g_serverWorld.zoneLoaded) return;
    ApplyAwards();
}

} // namespace net
