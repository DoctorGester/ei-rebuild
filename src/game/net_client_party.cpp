// The client's trade offer (0041DA50, called by the party screens after a change): one member
// message per party member, kept in our PartyMessage at +0x260C. See net_messages.h and
// docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"
#include "item.h"
#include "party.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_gameplay.h"

#include <cstring>

namespace net {

namespace {

// STLport vector<RefPtr> plumbing (library instantiations) and the message stream constructor.
void* __fastcall VectorGetAllocator(RefVector* a0, void*, void* a2) { return VectorGetAllocatorOriginal((const void*)a0, nullptr, a2); }
__declspec(naked) RefPtr* __fastcall VectorErase(RefVector* /*vector*/, void*, RefPtr* /*first*/, RefPtr* /*last*/) EI_ORIGINAL(0x00421ED0)
__declspec(naked) void __fastcall RefPtrConstructCopy(RefPtr* /*at*/, const RefPtr* /*from*/) EI_ORIGINAL(0x00429060)
__declspec(naked) void __fastcall VectorInsertOverflow(RefVector* /*vector*/, void*, RefPtr* /*pos*/, const RefPtr* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00426F00)
__declspec(naked) RefPtr* __fastcall RefPtrCopyRange(RefPtr* /*first*/, RefPtr* /*last*/, RefPtr* /*result*/) EI_ORIGINAL(0x00429080)
__declspec(naked) void __fastcall VectorDeallocate(RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426490)
__declspec(naked) void* __fastcall MemoryStreamConstructAt(CMemoryStream* /*stream*/) EI_ORIGINAL(0x0041DD70)

// vector<CEngineObject*> with counted elements (library), the +0x2614 list erase, the party's
// reset and add (00661050 class, outside this range).
__declspec(naked) void __fastcall ObjectVectorConstruct(void* /*vector*/, void*, const void* /*allocator*/) EI_ORIGINAL(0x00424B40)
__declspec(naked) void __fastcall ObjectPtrConstructCopy(CEngineObject** /*at*/, void*, CEngineObject* const* /*from*/) EI_ORIGINAL(0x004298F0)
__declspec(naked) void __fastcall ObjectVectorInsertOverflow(void* /*vector*/, void*, CEngineObject** /*pos*/, CEngineObject* const* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00428A20)
__declspec(naked) void __fastcall ObjectVectorDeallocate(CEngineObject** /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x004268A0)
__declspec(naked) void** __fastcall List2614Erase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00429770)
inline void __fastcall PartyReset(void* a0, void*, int a2, int a3) { PartyResetOriginal((void*)a0, nullptr, a2, a3); }
inline int __fastcall PartyAdd(void* a0, void*, void* a2, int a3, int a4) { return (int)PartyAddItemOriginal((void*)a0, nullptr, a2, a3, a4); }

// The received offers (+0x2610): an STLport list of counted PartyMessages.
using OfferNode = stlp::list<RefPtr>::Node;

void WriteDword(CStream* stream, std::uint32_t* scratch, std::uint32_t value) {
    *scratch = value;
    stream->Write(scratch, 4u);
}
CStream* StreamOf(CRefObject* member) { return &static_cast<PartyMemberMessage*>(member)->stream_; }

void PushBack(RefVector* v, const RefPtr* value) {
    if (v->end() != v->EndOfStorage()) {
        RefPtrConstructCopy(v->end(), value);
        ++v->Finish();
    } else {
        VectorInsertOverflow(v, nullptr, v->end(), value, 1);
    }
}

} // namespace

// 00429FC0: the first element in [first, last) whose member stream holds the same bytes as
// `message`'s (sizes through slot 4, then a byte compare). The last argument is not used.
RefPtr* __fastcall FindSameMessage(RefPtr* first, RefPtr* last, PartyMemberMessage* message, std::uint32_t) {
    CStream* stream = &message->stream_;
    for (; first != last; ++first) {
        CStream* element = StreamOf(first->ptr);
        std::uint32_t size = element->Size();
        if (stream->Size() != size) continue;
        const void* a = static_cast<CMemoryStream*>(element)->data_;
        const void* b = message->stream_.data_;
        if (std::memcmp(a, b, stream->Size()) == 0) break;
    }
    return first;
}
EI_REPLACE(0x00429FC0, FindSameMessage);

// 0041DA50: rebuilds the member messages of our offer (+0x260C) from our party (+0x2620): one per
// item of the party. Each gives its spell's id, type and packed id and the spell's Save (slot 10),
// or -1 without a spell, then the item's type and packed id and its own Save. A message equal to
// one of the previous messages is replaced by that one (so unchanged members keep their object).
// The offer takes the party's money; then its counters move on and its accepted flag is cleared.
void __fastcall ClientPublishParty(ClientSession* self) {
    CParty* party = self->parties_[0].Party();
    // Read again at each use, as the original does.
    auto state = [self] { return static_cast<PartyMessage*>(self->party_.ptr); };
    if (!state()) return;
    RefVector* messages = &state()->members_;

    // A copy of the old messages, then the list emptied.
    std::uint8_t allocator = 0;
    const void* allocatorCopy = VectorGetAllocator(messages, nullptr, &allocator);
    RefVector previous;
    VectorBaseConstruct(&previous, nullptr, static_cast<std::size_t>(messages->size()), allocatorCopy);
    previous.Finish() = UninitializedCopy(messages->begin(), messages->end(), previous.begin(), 0);
    VectorErase(&state()->members_, nullptr, state()->members_.begin(), state()->members_.end());
    state()->money_ = static_cast<std::uint32_t>(party->Money());

    std::uint32_t scratch = 0;
    for (int i = 0; i < party->items_.size; ++i) {
        auto* message = NewMessageMemory<PartyMemberMessage>();
        if (message) {
            MessageConstructBase(message);
            MemoryStreamConstructAt(&message->stream_);
            ei::SetVtable(message, PartyMemberMessage::kVtableAddress);
        }
        RefPtr ref;
        RefPtrFromRawOriginal(&ref, nullptr, message);
        CStream* stream = StreamOf(ref.ptr);
        auto* item = static_cast<CItem*>(party->items_[i]);
        stream->Clear();
        CSpell* spell = item->spell_;
        if (spell) {
            WriteDword(stream, &scratch, static_cast<std::uint32_t>(spell->id_));
            WriteDword(stream, &scratch, static_cast<std::uint32_t>(spell->type_));
            WriteDword(stream, &scratch, spell->unknown_18);
            spell->Save(stream);
        } else {
            WriteDword(stream, &scratch, 0xFFFFFFFFu);
        }
        WriteDword(stream, &scratch, static_cast<std::uint32_t>(item->type_));
        WriteDword(stream, &scratch, item->unknown_18);
        item->Save(stream);

        RefPtr* same = FindSameMessage(previous.begin(), previous.end(), static_cast<PartyMemberMessage*>(ref.ptr), scratch);
        PartyMessage* current = state();
        if (same != previous.end()) {
            PushBack(&current->members_, same);
            if (same + 1 != previous.end()) RefPtrCopyRange(same + 1, previous.end(), same);
            --previous.Finish();
            RefPtrDestroyAt(previous.end(), nullptr, 0);
        } else {
            PushBack(&current->members_, &ref);
        }
        if (ref.ptr) ref.ptr->Release();
    }

    ++state()->revision_;
    state()->accepted_ = 0;
    ++state()->version_;
    for (RefPtr* p = previous.begin(); p != previous.end(); ++p)
        if (p->ptr) p->ptr->Release();
    VectorDeallocate(previous.begin(), static_cast<std::size_t>(previous.EndOfStorage() - previous.begin()));
}
EI_REPLACE(0x0041DA50, ClientPublishParty);

// 0041E220: the received offer (list +0x2610) that pairs with ours (+0x260C): its partner is our
// player and its player our partner, and it is live. Null without one.
void* __fastcall ClientFindParty(ClientSession* self) {
    auto* ours = static_cast<PartyMessage*>(self->party_.ptr);
    if (!ours) return nullptr;
    auto* sentinel = static_cast<OfferNode*>(self->offers_);
    for (OfferNode* node = sentinel->next; node != sentinel; node = node->next) {
        auto* other = static_cast<PartyMessage*>(node->data.ptr);
        if (other->partner_.ptr != ours->player_.ptr || other->player_.ptr != ours->partner_.ptr) continue;
        if (other && !(other->flags7_ & CRefObject::kDetached)) return other;
    }
    return nullptr;
}
EI_REPLACE(0x0041E220, ClientFindParty);

// 0041E180: an accepted offer is unaccepted with its version moved on, so it is sent again.
void __fastcall ClientCommitParty(ClientSession* self) {
    auto* state = static_cast<PartyMessage*>(self->party_.ptr);
    if (!state || !state->accepted_) return;
    state->accepted_ = 0;
    ++static_cast<PartyMessage*>(self->party_.ptr)->version_;
}
EI_REPLACE(0x0041E180, ClientCommitParty);

// 0041DDC0: rebuilds the partner's party (+0x2690) from the paired offer, once every member message
// of it holds data. Each message gives (when its first dword, the spell's id, is positive) a spell
// made by the party's spell manager (slot 22; CreateObject with the stored type and that id, its
// packed id set and its Load, slot 9) and an item made by the item manager (slot 18; CreateObject
// with id -1, packed id, Load). The items are added to the party (00661DB0 with 1, 1), which takes
// the offer's money first; trade records older than the offer's revision are dropped.
void __fastcall ClientApplyParty(ClientSession* self) {
    auto* remote = static_cast<PartyMessage*>(ClientFindParty(self));
    if (!remote) return;
    auto count = [remote] { return static_cast<std::uint32_t>(remote->members_.size()); };
    for (std::uint32_t i = 0; i < count(); ++i) {
        CRefObject* message = remote->members_.begin()[i].ptr;
        if (!message || (message->flags7_ & CRefObject::kDetached)) return;
        if (StreamOf(message)->Size() == 0) return;
    }

    CParty* party = self->parties_[1].Party();
    PartyReset(party, nullptr, 1, 0);
    party->Money() = static_cast<int>(remote->money_);
    stlp::vector<CEngineObject*> items;
    std::uint8_t allocator = 0;
    ObjectVectorConstruct(&items, nullptr, &allocator);
    for (std::uint32_t i = 0; i < count(); ++i) {
        CStream* stream = StreamOf(remote->members_.begin()[i].ptr);
        stream->Seek(0);
        std::int32_t id;
        std::uint32_t type, packedId;
        stream->Read(&id, 4u);
        if (id > 0) {
            stream->Read(&type, 4u);
            stream->Read(&packedId, 4u);
            CEngineObject* spell = party->SpellManager()->CreateObject(static_cast<int>(type), id);
            spell->unknown_18 = packedId;
            spell->Load(stream);
        }
        stream->Read(&type, 4u);
        stream->Read(&packedId, 4u);
        CEngineObject* item = party->ItemManager()->CreateObject(static_cast<int>(type), -1);
        item->unknown_18 = packedId;
        item->Load(stream);
        CEngineObject* held = item;
        if (item) ++item->refCount_;
        if (items.Finish() != items.EndOfStorage()) {
            if (items.Finish()) ObjectPtrConstructCopy(items.Finish(), nullptr, &held);
            ++items.Finish();
        } else {
            ObjectVectorInsertOverflow(&items, nullptr, items.Finish(), &held, 1);
        }
        if (held) held->Release();
    }
    for (std::uint32_t i = 0; i < count(); ++i) PartyAdd(party, nullptr, items[i], 1, 1);

    void* entries = &self->tradeRecords_;
    std::int32_t revision = static_cast<std::int32_t>(remote->revision_);
    TradeRecordNode* sentinel = self->tradeRecords_;
    for (TradeRecordNode* node = sentinel->next; node != sentinel;) {
        TradeRecordNode* next = node->next;
        if (static_cast<std::int32_t>(node->acceptedRevision) < revision) {
            void* erased;
            List2614Erase(entries, nullptr, &erased, node);
        }
        node = next;
    }
    for (CEngineObject** p = items.begin(); p != items.end(); ++p)
        if (*p) (*p)->Release();
    ObjectVectorDeallocate(items.begin(), static_cast<std::size_t>(items.EndOfStorage() - items.begin()));
}
EI_REPLACE(0x0041DDC0, ClientApplyParty);

} // namespace net
