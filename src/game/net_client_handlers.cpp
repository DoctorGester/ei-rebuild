// The game client's message handlers (registered by the constructor) and the three party-screen entry points
// around them. Each keeps the replicated objects the server sends in one of the client's lists and tells the
// interface or the game flow. The messages are declared in net_messages.h. See docs/notes/network.md.
#include "address_order.h"
#include "library_containers.h"
#include "net_messages.h"
#include "party.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "record_lists.h"
#include "stl_sorts.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "world_map_files.h"
#include "game_singletons.h"

#include <cstring>


namespace net {

void __fastcall ClientApplyParty(ClientSession* self);      // net_client_party.cpp

namespace {

inline void FlowVariablesChanged(void* flow) { static_cast<GameFlow*>(flow)->UpdateFromServer(); }
inline void __fastcall FlowLoadZoneQuests(void*, void*, const char* a2) { LoadZoneQuests(a2); }

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
template <typename T>
struct SlotOf {
    using Type = T;
};
// The counted slot `slot` holds `object` (one more reference); the previous object released.
template <typename T>
void AssignRef(T** slot, typename SlotOf<T>::Type* object) {
    T*& holder = *slot;
    T* previous = holder;
    holder = object;
    if (object) ++object->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
}
void AssignRef(RefPtr* slot, CRefObject* object) { AssignRef(&slot->ptr, object); }
template <typename T>
void ClearRef(T** slot) {
    T*& holder = *slot;
    T* previous = holder;
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

// Every request for `file` erased (the sentinel read once, the next node taken first).
void DropRequestsFor(ClientSession* self, const Message* file) {
    RefNode* sentinel = self->fileRequests_.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (self->fileRequests_.At(node)->FileRef().ptr == file) self->fileRequests_.Erase(node);
        node = next;
    }
}

// The owner's release of the party description as the handlers inline it, then null.
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

// Records the trade the paired offer stands for: our offer accepts the other's current
// revision (and changes), and unless a record of the two revisions exists one is added with copies
// of the two parties (parties_) and `party`.
void ClientSession::RecordTrade(const void* party) {
    PartyMessage* remote = ClientFindParty(this);
    if (!remote) return;
    if (!party_.ptr) return;
    auto* ours = party_.Get();
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
    record->parties[0]->Assign(parties_[0].Get());
    record->parties[1]->Assign(parties_[1].Get());
    record->parties[2]->Assign(static_cast<CParty*>(const_cast<void*>(party)));
}
EI_REWRITTEN(0x0041E0A0, ClientSession::RecordTrade);

// Starts our trade offer to `partner` when the binding and its player are
// live: kept, from the binding's player.
void ClientSession::StartParty(CRefObject* partner) {
    BindingMessage* binding = binding_;
    if (!IsLive(binding)) return;
    if (!IsLive(binding->PlayerRef().ptr)) return;
    auto* party = NewCounted<PartyMessage>();
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
    AssignRef(&party_.Get()->player_, binding_->PlayerRef().ptr);
    AssignRef(&party_.Get()->partner_, partner);
}
EI_REWRITTEN(0x0041E270, ClientSession::StartParty);

// Drops the party description.
void __fastcall ClientDropParty(ClientSession* self) { DropParty(self); }
EI_REWRITTEN(0x0041E3E0, ClientDropParty);


// The binding of this client: kept, dropped. An update lists its
// objects in the order of the server's entries' addresses (Connection::SendUpdate), so the players may
// come before the binding; their refresh then listed nobody (ShowPlayers lists only with the binding)
// and the original's binding does not refresh: the base screen's faces followed the heap. Ours
// (address_order.h) refreshes the list when players came before the binding, as if it came first.
void __fastcall ClientOnBinding(ClientSession* self, BindingMessage* message) {
    AssignRef(&self->binding_, message);
    if (!self->players_.Empty()) TheInterface()->OnPlayersChanged(1);
}
void __fastcall ClientOnBindingGone(ClientSession* self, Message*) { ClearRef(&self->binding_); }
// The players, with the interface's player list refreshed.
void __fastcall ClientOnPlayer(ClientSession* self, Message* message) {
    AppendRaw(self->players_, message);
    TheInterface()->OnPlayersChanged(1);
}
void __fastcall ClientOnPlayerChanged(ClientSession*, Message*) { TheInterface()->OnPlayersChanged(0); }
void __fastcall ClientOnPlayerGone(ClientSession* self, Message* message) {
    RemoveCounted(self->players_, message);
    TheInterface()->OnPlayersChanged(1);
}
// The chat lines, each shown with its number.
void __fastcall ClientOnLine(ClientSession* self, LineMessage* message) {
    AppendCounted(self->lines_, message);
    TheInterface()->OnChatLine(message->text_.data(), message->number_);
}
void __fastcall ClientOnLineGone(ClientSession* self, Message* message) { RemoveCounted(self->lines_, message); }
// The host message kept.
void __fastcall ClientOnInfo(ClientSession* self, HostMessage* message) { AssignRef(&self->info_, message); }
void __fastcall ClientOnInfoGone(ClientSession* self, Message*) { ClearRef(&self->info_); }
// The zones offered, passed to the game flow.
void __fastcall ClientOnZone(ClientSession* self, ZoneMessage* message) {
    AppendCounted(self->zones_, message);
    TheGameFlow()->MarkBriefing(message->text_.data());
}
void __fastcall ClientOnZoneGone(ClientSession* self, ZoneMessage* message) {
    RemoveCounted(self->zones_, message);
    TheGameFlow()->ForgetBriefing(message->text_.data());
}
// The current map; without a server of our own the flow loads it.
void __fastcall ClientOnMap(ClientSession* self, MapMessage* message) {
    AssignRef(&self->map_, message);
    auto* server = GameServer();
    if (server && !(server->flags7_ & CRefObject::kDetached)) return;
    FlowLoadZoneQuests(TheGameFlow(), nullptr, message->text_.data());
    TheGameFlow()->RegisterQuests(message->text_.data());
}
void __fastcall ClientOnMapGone(ClientSession* self, Message*) { ClearRef(&self->map_); }
// The script variables; the flow is told of every change.
void __fastcall ClientOnVariable(ClientSession* self, Message* message) {
    AppendRaw(self->variables_, message);
    FlowVariablesChanged(TheGameFlow());
}
void __fastcall ClientOnVariableChanged(ClientSession*, Message*) { FlowVariablesChanged(TheGameFlow()); }
void __fastcall ClientOnVariableGone(ClientSession* self, Message* message) {
    RemoveRaw(self->variables_, message);
    FlowVariablesChanged(TheGameFlow());
}
// The shared files; for a file the flow lacks, a request holding the file goes on fileRequests_.
void __fastcall ClientOnFile(ClientSession* self, FileMessage* message) {
    AppendRaw(self->files_, message);
    if (MapFileExists(message->name_.data())) return;
    auto* request = NewCounted<FileRequestMessage>();
    AssignRef(&request->FileRef(), message);
    AppendRaw(self->fileRequests_, request);
}
void __fastcall ClientOnFileGone(ClientSession* self, Message* message) {
    RemoveRaw(self->files_, message);
    DropRequestsFor(self, message);
}
// A chunk of a shared file. When chunks_ then holds as many chunks of the file as the file message announces,
// they are sorted by index, their 64 bytes each joined in a growing buffer cut to the file's size, the game
// flow writes the buffer as the map file of that name, and the requests for the file leave fileRequests_. A
// file without a name is only collected. Rebirth's MQClient returns after collecting every chunk: no file from
// the server is written.
void __fastcall ClientOnFileChunk(ClientSession* self, FileChunkMessage* message) {
    AppendRaw(self->chunks_, message);
    auto* file = message->file_.Get();
    const char* none = game::EmptyText();
    const std::size_t noneLength = std::strlen(none);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::MQClient)) return;
    if (file->name_.size() == noneLength && std::memcmp(file->name_.data(), none, noneLength) == 0)
        return;

    stlp::vector<FileChunkMessage*> chunks;
    int count = 0;
    for (RefNode* node = self->chunks_.First(); node != self->chunks_.End(); node = node->next) {
        auto* chunk = self->chunks_.At(node);
        if (chunk->file_.ptr != file) continue;
        ++count;
        chunks.push_back(chunk);
    }
    if (count == file->chunkCount_) {
        GrowingBuffer buffer;
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
                buffer.WriteMore(data, FileChunkMessage::kDataSize);
            }
        }
        // The buffer rewound and its written length set to the file's (the last chunk is padded).
        buffer.cur = buffer.begin;
        const std::int32_t size = file->size_;
        buffer.high = buffer.begin + size;
        if (buffer.high > buffer.end) buffer.Grow(static_cast<std::uint32_t>(size));
        WriteMapFile(file->name_.data(), &buffer);
        DropRequestsFor(self, file);
    }
    chunks.FreeStorage();
}
EI_REPLACE(0x0041ECC0, ClientOnFileChunk);
void __fastcall ClientOnObjectGone(ClientSession* self, Message* message) { RemoveRaw(self->chunks_, message); }
// The awards (applied by the tick).
void __fastcall ClientOnAward(ClientSession* self, Message* message) { AppendRaw(self->awards_, message); }
// The other players' trade offers; the one paired with ours rebuilds the party.
void __fastcall ClientOnParty(ClientSession* self, Message* message) {
    AppendRaw(self->offers_, message);
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyChanged(ClientSession* self, Message* message) {
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyGone(ClientSession* self, Message* message) { RemoveRaw(self->offers_, message); }
// Party members, every channel: the description committed and the party rebuilt.
void __fastcall ClientOnMember(ClientSession* self, Message*) {
    ClientCommitParty(self);
    ClientApplyParty(self);
}
// The trade: a new one empties the trade records and resets both parties.
void __fastcall ClientOnTrade(ClientSession* self, TradeMessage* message) {
    AssignRef(&self->trade_, message);
    ClearTradeRecords(self);
    reinterpret_cast<CParty*>(&self->parties_[0])->Clear(1, 0);
    reinterpret_cast<CParty*>(&self->parties_[1])->Clear(1, 0);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade,
// the party description and the records are dropped.
void __fastcall ClientOnTradeChanged(ClientSession* self, TradeMessage* message) {
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
            TheInterface()->OnExchangeDone(record.parties[1].Get(), record.parties[0].Get(), record.parties[2].Get());
            break;
        }
    }
    ClearRef(&self->trade_);
    DropParty(self);
    ClearTradeRecords(self);
}
void __fastcall ClientOnTradeGone(ClientSession* self, Message* message) {
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
