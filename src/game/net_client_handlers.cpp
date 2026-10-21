// The game client's message handlers (registered by the constructor 0041BC40, __fastcall with the
// client in ECX and the message in EDX) and the three party-screen entry points around them
// (0041E0A0, 0041E270, 0041E3E0). Each keeps the replicated objects the server sends in one of the
// client's lists and tells the interface or the game flow. 0041ECC0 (the file chunk handler for
// channel 0) is patched by Rebirth: ours installs in the vanilla launch only. The messages are
// declared in net_messages.h. See docs/notes/network.md.
#include "library_containers.h"
#include "net_messages.h"
#include "party.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_world.h"
#include "record_lists.h"
#include "stl_sorts.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "game_singletons.h"

#include <cstring>


namespace net {

void __fastcall ClientApplyParty(ClientSession* self);      // net_client_party.cpp

namespace {

// std::sort of the chunk pointers by index, as the instantiation splits it: the introsort loop
// (RET 0xC: a null type tag, twice the size's log2, the empty comparator) and the final insertion
// sort (RET 4: the comparator).
// The game flow's map file writer (patched by Rebirth: through its address).
__declspec(naked) void __fastcall FlowWriteMapFile(void* /*flow*/, void*, const char* /*name*/, GameBuffer* /*buffer*/)
    EI_ORIGINAL(0x005813A0)
// Parties (00661050 class, outside this range).
inline void __fastcall PartyReset(void* a0, void*, int a2, int a3) { PartyResetOriginal((void*)a0, nullptr, a2, a3); }
// The interface's text table (0x7C7D50) and the game flow (0x7AFE20), outside this range.
inline void FlowVariablesChanged(void* flow) { FlowUpdateFromServerOriginal(flow); }
__declspec(naked) bool __fastcall FlowHasFile(void* /*flow*/, void*, const char* /*name*/) EI_ORIGINAL(0x005812F0)
inline void __fastcall FlowLoadZoneQuests(void*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }

bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

// Every node holding `value` erased (the sentinel read once, the next node taken first).
template <typename List>
void RemoveValue(List& list, const void* value) {
    RefNode* sentinel = list.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (node->data.ptr == value) list.Erase(node);
        node = next;
    }
}
// A list given the message through a holder without a reference.
template <typename List>
void AppendRaw(List& list, Message* message) {
    RefPtr raw;
    list.Insert(list.End(), *RefPtrFromRaw(&raw, message));
}
template <typename List>
void RemoveRaw(List& list, Message* message) {
    RefPtr raw;
    RemoveValue(list, RefPtrFromRaw(&raw, message)->ptr);
}

// Counted pointers as the handlers inline them.
void AddRef(CRefObject* object) {
    if (object) ++object->refs_;
}
void ReleaseRef(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}
void AssignRef(CRefObject** slot, CRefObject* object) {
    CRefObject*& holder = *slot;
    CRefObject* previous = holder;
    holder = object;
    if (object) ++object->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
}
void AssignRef(RefPtr* slot, CRefObject* object) { AssignRef(&slot->ptr, object); }
void ClearRef(CRefObject** slot) {
    CRefObject*& holder = *slot;
    CRefObject* previous = holder;
    holder = nullptr;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
}
// A list appended with a counted holder of the message that is released afterwards.
template <typename List>
void AppendCounted(List& list, Message* message) {
    CRefObject* object = message;
    RefPtr local;
    local.ptr = object;
    AddRef(object);
    list.Insert(list.End(), local);
    ReleaseRef(local.ptr);
}
template <typename List>
void RemoveCounted(List& list, Message* message) {
    CRefObject* object = message;
    AddRef(object);
    RemoveValue(list, object);
    ReleaseRef(object);
}

// Every request for `file` erased from +0x25E8 (the sentinel read once, the next node taken first).
void DropRequestsFor(ClientSession* self, const Message* file) {
    RefNode* sentinel = self->fileRequests_.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (static_cast<FileRequestMessage*>(node->data.ptr)->FileRef().ptr == file) self->fileRequests_.Erase(node);
        node = next;
    }
}

// The owner's release of the party description (+0x260C) as the handlers inline it, then null.
void DropParty(ClientSession* self) {
    auto* state = self->party_.ptr;
    if (!state) return;
    if (state->refs_ != 1) {
        state->flags7_ |= CRefObject::kDetached;
        ++state->refs_;
        state->OnShutdown();
        --state->refs_;
    }
    auto* held = self->party_.ptr;
    --held->refs_;
    if (held->refs_ == 0 && held) delete held;
    self->party_.ptr = nullptr;
}

// The trade records emptied (clear() inlined).
void ClearTradeRecords(ClientSession* self) { self->tradeRecords_.Clear<recordlist::TradeElement>(); }

} // namespace

// --- The party-screen entry points -------------------------------------------------------------

// 0041E0A0: records the trade the paired offer stands for: our offer accepts the other's current
// revision (and changes), and unless a record of the two revisions exists one is added with copies
// of the two parties (+0x2620, +0x2690) and `party`.
void ClientSession::RecordTrade(const void* party) {
    auto* remote = static_cast<PartyMessage*>(ClientFindParty(this));
    if (!remote) return;
    if (!party_.ptr) return;
    auto* ours = static_cast<PartyMessage*>(party_.ptr);
    ours->accepted_ = 1;
    ours->acceptedRevision_ = remote->revision_;
    ++ours->version_;
    std::uint32_t a = ours->acceptedRevision_;
    std::uint32_t b = ours->revision_;
    for (TradeRecordNode* node = tradeRecords_.First(); node != tradeRecords_.End(); node = node->next)
        if (node->data.revision == b && node->data.acceptedRevision == a) return;
    stlp::list<TradeRecord>::InsertDefault<recordlist::TradeElement>(tradeRecords_.End());
    TradeRecord* record = &tradeRecords_.End()->prev->data;   // the node just added
    record->acceptedRevision = ours->acceptedRevision_;
    record->revision = ours->revision_;
    record->parties[0].Party()->Assign(parties_[0].Party());   // 00661300
    record->parties[1].Party()->Assign(parties_[1].Party());
    record->parties[2].Party()->Assign(static_cast<CParty*>(const_cast<void*>(party)));
}
EI_REWRITTEN(0x0041E0A0, ClientSession::RecordTrade);

// 0041E270: starts our trade offer to `partner` when the binding (+0x25F0) and its player are
// live: kept at +0x260C, from the binding's player.
void ClientSession::StartParty(CRefObject* partner) {
    auto* binding = static_cast<BindingMessage*>(binding_);
    if (!Live(binding)) return;
    if (!Live(binding->PlayerRef().ptr)) return;
    auto* party = NewMessageMemory<PartyMessage>();
    if (party) {
        RefValueConstructBase(party);
        ei::SetVtable(party, kMessageVtable);
        party->version_ = 1;
        party->members_.ConstructEmpty();   // 0041FFF0
        party->player_.ptr = nullptr;
        party->partner_.ptr = nullptr;   // RefPtr() (0041FF80)
        ei::SetVtable(party, PartyMessage::kVtableAddress);
        party->acceptedRevision_ = 0;
        party->revision_ = 1;
        party->accepted_ = 0;
        party->money_ = 0;
        party->trading_ = 0;
    }
    auto* holder = &party_;
    if (party != holder->ptr) {
        if (CRefObject* old = holder->ptr) {
            if (old->refs_ != 1) {
                old->flags7_ |= CRefObject::kDetached;
                old->Shutdown();
            }
            holder->ptr->Release();
        }
        holder->Share(party);
    }
    AssignRef(&static_cast<PartyMessage*>(party_.ptr)->player_, static_cast<BindingMessage*>(binding_)->PlayerRef().ptr);
    AssignRef(&static_cast<PartyMessage*>(party_.ptr)->partner_, partner);
}
EI_REWRITTEN(0x0041E270, ClientSession::StartParty);

// 0041E3E0: drops the party description.
void __fastcall ClientDropParty(ClientSession* self) { DropParty(self); }
EI_REWRITTEN(0x0041E3E0, ClientDropParty);

// --- Handlers ----------------------------------------------------------------------------------

// The binding of this client: kept at +0x25F0 (0041E430), dropped (0041E460).
void __fastcall ClientOnBinding(ClientSession* self, Message* message) { AssignRef(&self->binding_, message); }
void __fastcall ClientOnBindingGone(ClientSession* self, Message*) { ClearRef(&self->binding_); }
// The players on +0x25D8, with the interface's player list refreshed.
void __fastcall ClientOnPlayer(ClientSession* self, Message* message) {   // 0041E490
    AppendRaw(self->players_, message);
    TheInterface()->OnPlayersChanged(1);
}
void __fastcall ClientOnPlayerChanged(ClientSession*, Message*) { TheInterface()->OnPlayersChanged(0); }   // 0041E510
void __fastcall ClientOnPlayerGone(ClientSession* self, Message* message) {   // 0041E520
    RemoveCounted(self->players_, message);
    TheInterface()->OnPlayersChanged(1);
}
// The chat lines on +0x25DC, each shown with its number.
void __fastcall ClientOnLine(ClientSession* self, LineMessage* message) {   // 0041E5D0
    AppendCounted(self->lines_, message);
    TheInterface()->OnChatLine(message->text_.data(), message->number_);
}
void __fastcall ClientOnLineGone(ClientSession* self, Message* message) { RemoveCounted(self->lines_, message); }   // 0041E690
// The host message kept at +0x25EC.
void __fastcall ClientOnInfo(ClientSession* self, Message* message) { AssignRef(&self->info_, message); }   // 0041E730
void __fastcall ClientOnInfoGone(ClientSession* self, Message*) { ClearRef(&self->info_); }   // 0041E760
// The zones offered on +0x25F8, passed to the game flow.
void __fastcall ClientOnZone(ClientSession* self, ZoneMessage* message) {   // 0041E790
    AppendCounted(self->zones_, message);
    TheGameFlow()->MarkBriefing(message->text_.data());
}
void __fastcall ClientOnZoneGone(ClientSession* self, ZoneMessage* message) {   // 0041E850
    RemoveCounted(self->zones_, message);
    TheGameFlow()->ForgetBriefing(message->text_.data());
}
// The current map at +0x25F4; without a server of our own the flow loads it.
void __fastcall ClientOnMap(ClientSession* self, MapMessage* message) {   // 0041E900
    AssignRef(&self->map_, message);
    auto* server = GameServer();
    if (server && !(server->flags7_ & CRefObject::kDetached)) return;
    FlowLoadZoneQuests(TheGameFlow(), nullptr, message->text_.data());
    TheGameFlow()->RegisterQuests(message->text_.data());
}
void __fastcall ClientOnMapGone(ClientSession* self, Message*) { ClearRef(&self->map_); }   // 0041E960
// The script variables on +0x25FC; the flow is told of every change.
void __fastcall ClientOnVariable(ClientSession* self, Message* message) {   // 0041E990
    AppendRaw(self->variables_, message);
    FlowVariablesChanged(TheGameFlow());
}
void __fastcall ClientOnVariableChanged(ClientSession*, Message*) { FlowVariablesChanged(TheGameFlow()); }   // 0041EA10
void __fastcall ClientOnVariableGone(ClientSession* self, Message* message) {   // 0041EA20
    RemoveRaw(self->variables_, message);
    FlowVariablesChanged(TheGameFlow());
}
// The shared files on +0x25E0; for a file the flow lacks, a request holding the file goes on
// +0x25E8.
void __fastcall ClientOnFile(ClientSession* self, FileMessage* message) {   // 0041EAC0
    AppendRaw(self->files_, message);
    if (FlowHasFile(TheGameFlow(), nullptr, message->name_.data())) return;
    auto* request = NewMessageMemory<FileRequestMessage>();
    if (request) {
        RefValueConstructBase(request);
        ei::SetVtable(request, kMessageVtable);
        request->version_ = 1;
        request->FileRef().ptr = nullptr;
        request->ConnectionRef().ptr = nullptr;   // RefPtr() (0041FF70)
        ei::SetVtable(request, FileRequestMessage::kVtableAddress);
    }
    AssignRef(&request->FileRef(), message);
    AppendRaw(self->fileRequests_, request);
}
void __fastcall ClientOnFileGone(ClientSession* self, Message* message) {   // 0041EBF0
    RemoveRaw(self->files_, message);
    DropRequestsFor(self, message);
}
// 0041ECC0 (A-E2): a chunk of a shared file, kept on +0x25E4. When the list then holds as many
// chunks of the file as the file message announces, they are sorted by index, their 64 bytes each
// joined in a growing buffer cut to the file's size, the game flow writes the buffer as the map
// file of that name, and the requests for the file leave +0x25E8. A file without a name (the text
// at 0079B298) is only collected. Rebirth's MQClient (the JNE at 0041ED35 aimed at the next
// instruction, the JE at 0041ED40 made a JMP) returns after collecting every chunk: no file from the
// server is written. The constructor registers the handler by its address.
void __fastcall ClientOnFileChunk(ClientSession* self, FileChunkMessage* message) {
    AppendRaw(self->chunks_, message);
    auto* file = static_cast<FileMessage*>(message->file_.ptr);
    const char* none = game::EmptyText();
    const std::size_t noneLength = std::strlen(none);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::MQClient)) return;
    if (file->name_.size() == noneLength && std::memcmp(file->name_.data(), none, noneLength) == 0)
        return;

    stlp::vector<FileChunkMessage*> chunks;
    int count = 0;
    for (RefNode* node = self->chunks_.First(); node != self->chunks_.End(); node = node->next) {
        auto* chunk = static_cast<FileChunkMessage*>(node->data.ptr);
        if (chunk->file_.ptr != file) continue;
        ++count;
        chunks.push_back(chunk);
    }
    if (count == file->chunkCount_) {
        GameBuffer buffer;
        buffer.ConstructGrowing();
        // std::sort by the chunks' index (the instantiation's two halves).
        if (chunks.begin() != chunks.end()) {
            int depth = 0;
            for (int n = static_cast<int>(chunks.size()); n != 1; n >>= 1) ++depth;
            stl_sorts::ChunkIntroSort(chunks.begin(), chunks.end(), nullptr, depth * 2, 0);
            stl_sorts::ChunkFinalSort(chunks.begin(), chunks.end(), 0);
        }
        for (FileChunkMessage** chunk = chunks.begin(); chunk != chunks.end(); ++chunk) {
            const std::uint8_t* data = (*chunk)->data_;
            if (buffer.cur + FileChunkMessage::kDataSize <= buffer.end) {
                buffer.flags |= 1;
                std::memcpy(buffer.cur, data, FileChunkMessage::kDataSize);
                buffer.cur += FileChunkMessage::kDataSize;
            } else {
                buffer.WriteMoreGrowing(data, FileChunkMessage::kDataSize);
            }
        }
        // The buffer rewound and its written length set to the file's (the last chunk is padded).
        buffer.cur = buffer.begin;
        const std::int32_t size = file->size_;
        buffer.high = buffer.begin + size;
        if (buffer.high > buffer.end) buffer.Grow(static_cast<std::uint32_t>(size));
        FlowWriteMapFile(TheGameFlow(), nullptr, file->name_.data(), &buffer);
        DropRequestsFor(self, file);
        buffer.DestructGrowing();
    }
    chunks.FreeStorage();
}
EI_REPLACE(0x0041ECC0, ClientOnFileChunk);
void __fastcall ClientOnObjectGone(ClientSession* self, Message* message) { RemoveRaw(self->chunks_, message); }   // 0041EF10
// The awards on +0x2608 (applied by the tick).
void __fastcall ClientOnAward(ClientSession* self, Message* message) { AppendRaw(self->awards_, message); }   // 0041EF90
// The other players' trade offers on +0x2610; the one paired with ours rebuilds the party.
void __fastcall ClientOnParty(ClientSession* self, Message* message) {   // 0041EFF0
    AppendRaw(self->offers_, message);
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyChanged(ClientSession* self, Message* message) {   // 0041F090
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyGone(ClientSession* self, Message* message) { RemoveRaw(self->offers_, message); }   // 0041F0D0
// Party members, every channel: the description committed and the party rebuilt.
void __fastcall ClientOnMember(ClientSession* self, Message*) {   // 0041F150 / 0041F180 / 0041F1B0
    ClientCommitParty(self);
    ClientApplyParty(self);
}
// The trade at +0x2618: a new one empties the trade records and resets both parties.
void __fastcall ClientOnTrade(ClientSession* self, Message* message) {   // 0041F1E0
    AssignRef(&self->trade_, message);
    ClearTradeRecords(self);
    PartyReset(&self->parties_[0], nullptr, 1, 0);
    PartyReset(&self->parties_[1], nullptr, 1, 0);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade,
// the party description and the records are dropped.
void __fastcall ClientOnTradeChanged(ClientSession* self, TradeMessage* message) {   // 0041F270
    if (!message->done_ || message != self->trade_) return;
    std::uint32_t a, b;
    if (message->offer_.ptr == self->party_.ptr) {
        b = message->revision_;
        a = message->partnerRevision_;
    } else {
        b = message->partnerRevision_;
        a = message->revision_;
    }
    for (TradeRecordNode* node = self->tradeRecords_.First(); node != self->tradeRecords_.End(); node = node->next) {
        TradeRecord& record = node->data;
        if (record.revision == b && record.acceptedRevision == a) {
            TheInterface()->OnExchangeDone(&record.parties[1], &record.parties[0], &record.parties[2]);
            break;
        }
    }
    ClearRef(&self->trade_);
    DropParty(self);
    ClearTradeRecords(self);
}
void __fastcall ClientOnTradeGone(ClientSession* self, Message* message) {   // 0041F3A0
    if (self->trade_ != message) return;
    ClearRef(&self->trade_);
    DropParty(self);
    ClearTradeRecords(self);
}

EI_REPLACE(0x0041E430, ClientOnBinding);
EI_REPLACE(0x0041E460, ClientOnBindingGone);
EI_REPLACE(0x0041E490, ClientOnPlayer);
EI_REPLACE(0x0041E510, ClientOnPlayerChanged);
EI_REPLACE(0x0041E520, ClientOnPlayerGone);
EI_REPLACE(0x0041E5D0, ClientOnLine);
EI_REPLACE(0x0041E690, ClientOnLineGone);
EI_REPLACE(0x0041E730, ClientOnInfo);
EI_REPLACE(0x0041E760, ClientOnInfoGone);
EI_REPLACE(0x0041E790, ClientOnZone);
EI_REPLACE(0x0041E850, ClientOnZoneGone);
EI_REPLACE(0x0041E900, ClientOnMap);
EI_REPLACE(0x0041E960, ClientOnMapGone);
EI_REPLACE(0x0041E990, ClientOnVariable);
EI_REPLACE(0x0041EA10, ClientOnVariableChanged);
EI_REPLACE(0x0041EA20, ClientOnVariableGone);
EI_REPLACE(0x0041EAC0, ClientOnFile);
EI_REPLACE(0x0041EBF0, ClientOnFileGone);
EI_REPLACE(0x0041EF10, ClientOnObjectGone);
EI_REPLACE(0x0041EF90, ClientOnAward);
EI_REPLACE(0x0041EFF0, ClientOnParty);
EI_REPLACE(0x0041F090, ClientOnPartyChanged);
EI_REPLACE(0x0041F0D0, ClientOnPartyGone);
EI_REPLACE(0x0041F150, ClientOnMember);
EI_REPLACE(0x0041F180, ClientOnMember);
EI_REPLACE(0x0041F1B0, ClientOnMember);
EI_REPLACE(0x0041F1E0, ClientOnTrade);
EI_REPLACE(0x0041F270, ClientOnTradeChanged);
EI_REPLACE(0x0041F3A0, ClientOnTradeGone);

} // namespace net
