// The client's trade offer (called by the party screens after a change): one member
// message per party member, kept in our PartyMessage. See net_messages.h and docs/notes/network.md.
#include "core_refcount.h"
#include "core_streams.h"
#include "item.h"
#include "item_manager.h"
#include "net_messages.h"
#include "party.h"
#include "spell.h"

#include "lib/crt.h"

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
MemoryStream* StreamOf(RefObject* member) { return &static_cast<PartyMemberMessage*>(member)->stream; }

// The first element in [first, last) whose member stream holds the same bytes as
// `message`'s (sizes, then a byte compare).
std::vector<HeldRef<RefObject>>::iterator FindSameMessage(std::vector<HeldRef<RefObject>>::iterator first, std::vector<HeldRef<RefObject>>::iterator last, PartyMemberMessage* message) {
    ByteStream* stream = &message->stream;
    for (; first != last; ++first) {
        MemoryStream* element = StreamOf(first->Get());
        std::uint32_t size = element->Size();
        if (stream->Size() != size) continue;
        const void* a = element->data.data();
        const void* b = message->stream.data.data();
        if (std::memcmp(a, b, stream->Size()) == 0) break;
    }
    return first;
}

} // namespace

// Rebuilds the member messages of our offer from our party: one per
// item of the party. Each gives its spell's id, type and packed id and the spell's Save,
// or -1 without a spell, then the item's type and packed id and its own Save. A message equal to
// one of the previous messages is replaced by that one (so unchanged members keep their object).
// The offer takes the party's money; then its counters move on and its accepted flag is cleared.
void ClientPublishParty(ClientSession* self) {
    CParty* party = self->parties[0].Get();
    // Read again at each use.
    auto state = [self] { return self->party.Get(); };
    if (!state()) return;
    // A copy of the old messages, then the list emptied.
    std::vector<HeldRef<RefObject>> previous = state()->members;
    state()->members.clear();
    state()->money = static_cast<std::uint32_t>(party->money);

    std::uint32_t scratch = 0;
    for (int i = 0; i < std::ssize(party->items); ++i) {
        auto* message = NewCounted<PartyMemberMessage>();
        HeldRef<RefObject> ref(message);
        ByteStream* stream = &message->stream;
        Item* item = party->items[i];
        stream->Clear();
        CSpell* spell = item->spell;
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
        RefObject* message = remote->members[i].Get();
        if (!message || (message->flags7 & RefObject::kDetached)) return;
        if (StreamOf(message)->Size() == 0) return;
    }

    CParty* party = self->parties[1].Get();
    party->Clear(1, 0);
    party->money = static_cast<int>(remote->money);
    std::vector<EngineObject*> items;   // each holds a reference
    for (std::uint32_t i = 0; i < count(); ++i) {
        ByteStream* stream = StreamOf(remote->members[i].Get());
        stream->Seek(0);
        std::int32_t id;
        std::uint32_t type, packedId;
        stream->ReadValue(id);
        if (id > 0) {
            stream->ReadValue(type);
            stream->ReadValue(packedId);
            EngineObject* spell = party->SpellManager()->CreateObject(static_cast<int>(type), id);
            spell->prototypeId = packedId;
            spell->Load(stream);
        }
        stream->ReadValue(type);
        stream->ReadValue(packedId);
        EngineObject* item = party->ItemFactory()->CreateObject(static_cast<int>(type), -1);
        item->prototypeId = packedId;
        item->Load(stream);
        if (item) item->AddRef();
        items.push_back(item);
    }
    for (std::uint32_t i = 0; i < count(); ++i) party->AddItem(static_cast<Item*>(items[i]), 1, 1);

    std::int32_t revision = static_cast<std::int32_t>(remote->revision);
    std::list<TradeRecord>& records = self->tradeRecords;
    for (auto record = records.begin(); record != records.end();)
        record = static_cast<std::int32_t>(record->acceptedRevision) < revision ? records.erase(record) : std::next(record);
    for (EngineObject* item : items)
        if (item) item->Release();
}

} // namespace net
