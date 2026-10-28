// The game client's message handlers (registered by the constructor) and the three party-screen entry points
// around them. Each keeps the replicated objects the server sends in one of the client's lists and tells the
// interface or the game flow. The messages are declared in net_messages.h. See docs/notes/network.md.
#include "net_messages.h"
#include "party.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "world_map_files.h"
#include "game_singletons.h"

#include <algorithm>
#include <cstring>
#include <vector>


namespace net {

void ClientApplyParty(ClientSession* self);      // net_client_party.cpp

namespace {

inline void FlowVariablesChanged(void* flow) { static_cast<GameFlow*>(flow)->UpdateFromServer(); }
inline void FlowLoadZoneQuests(void*, const char* a2) { LoadZoneQuests(a2); }

// Every node holding `value` erased (the sentinel read once, the next node taken first).
template <typename List>
void RemoveValue(List& list, const void* value) {
    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        if (node->ptr == value) list.erase(node);
        node = next;
    }
}
// The message appended (the list's element takes its own reference or share).
template <typename List>
void Append(List& list, Message* message) {
    list.emplace_back(message);
}
template <typename List>
void RemoveRaw(List& list, Message* message) {
    RemoveValue(list, message);
}

// Counted pointers as the handlers inline them.
void AddRef(CRefObject* object) {
    if (object) ++object->refs_;
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
template <typename List>
void RemoveCounted(List& list, Message* message) {
    CRefObject* object = message;
    AddRef(object);
    RemoveValue(list, object);
    ReleaseCounted(object);
}

// Every request for `file` erased (the sentinel read once, the next node taken first).
void DropRequestsFor(ClientSession* self, const Message* file) {
    for (auto node = self->fileRequests_.begin(); node != self->fileRequests_.end();) {
        auto next = std::next(node);
        if (node->Get()->FileRef().ptr == file) self->fileRequests_.erase(node);
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

// The trade records emptied.
void ClearTradeRecords(ClientSession* self) { self->tradeRecords_.clear(); }

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
    for (const TradeRecord& record : tradeRecords_)
        if (record.revision == b && record.acceptedRevision == a) return;
    {
        // insert(end()): a blank record copied into the list, then the blank destroyed.
        const TradeRecord blank;
        tradeRecords_.push_back(blank);
    }
    TradeRecord& record = tradeRecords_.back();
    record.acceptedRevision = ours->acceptedRevision_;
    record.revision = ours->revision_;
    record.parties[0].Assign(parties_[0].Get());
    record.parties[1].Assign(parties_[1].Get());
    record.parties[2].Assign(static_cast<CParty*>(const_cast<void*>(party)));
}

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

// Drops the party description.
void ClientDropParty(ClientSession* self) { DropParty(self); }

// The binding of this client: kept, dropped. An update lists its
// objects in the order of the server's entries' addresses (Connection::SendUpdate), so the players may
// come before the binding; their refresh then listed nobody (ShowPlayers lists only with the binding)
// and the original's binding does not refresh: the base screen's faces followed the heap. Ours
// (address_order.h) refreshes the list when players came before the binding, as if it came first.
void ClientOnBinding(ClientSession* self, BindingMessage* message) {
    AssignRef(&self->binding_, message);
    if (!self->players_.empty()) g_interface->OnPlayersChanged(1);
}
void ClientOnBindingGone(ClientSession* self, Message*) { ClearRef(&self->binding_); }
// The players, with the interface's player list refreshed.
void ClientOnPlayer(ClientSession* self, Message* message) {
    Append(self->players_, message);
    g_interface->OnPlayersChanged(1);
}
void ClientOnPlayerChanged(ClientSession*, Message*) { g_interface->OnPlayersChanged(0); }
void ClientOnPlayerGone(ClientSession* self, Message* message) {
    RemoveCounted(self->players_, message);
    g_interface->OnPlayersChanged(1);
}
// The chat lines, each shown with its number.
void ClientOnLine(ClientSession* self, LineMessage* message) {
    Append(self->lines_, message);
    g_interface->OnChatLine(message->text_.data());
}
void ClientOnLineGone(ClientSession* self, Message* message) { RemoveCounted(self->lines_, message); }
// The host message kept.
void ClientOnInfo(ClientSession* self, HostMessage* message) { AssignRef(&self->info_, message); }
void ClientOnInfoGone(ClientSession* self, Message*) { ClearRef(&self->info_); }
// The zones offered, passed to the game flow.
void ClientOnZone(ClientSession* self, ZoneMessage* message) {
    Append(self->zones_, message);
    g_gameFlow->MarkBriefing(message->text_.data());
}
void ClientOnZoneGone(ClientSession* self, ZoneMessage* message) {
    RemoveCounted(self->zones_, message);
    g_gameFlow->ForgetBriefing(message->text_.data());
}
// The current map; without a server of our own the flow loads it.
void ClientOnMap(ClientSession* self, MapMessage* message) {
    AssignRef(&self->map_, message);
    auto* server = g_gameServer;
    if (server && !(server->flags7_ & CRefObject::kDetached)) return;
    FlowLoadZoneQuests(&g_gameFlow.Get(), message->text_.data());
    g_gameFlow->RegisterQuests(message->text_.data());
}
void ClientOnMapGone(ClientSession* self, Message*) { ClearRef(&self->map_); }
// The script variables; the flow is told of every change.
void ClientOnVariable(ClientSession* self, Message* message) {
    Append(self->variables_, message);
    FlowVariablesChanged(&g_gameFlow.Get());
}
void ClientOnVariableChanged(ClientSession*, Message*) { FlowVariablesChanged(&g_gameFlow.Get()); }
void ClientOnVariableGone(ClientSession* self, Message* message) {
    RemoveRaw(self->variables_, message);
    FlowVariablesChanged(&g_gameFlow.Get());
}
// The shared files; for a file the flow lacks, a request holding the file goes on fileRequests_.
void ClientOnFile(ClientSession* self, FileMessage* message) {
    Append(self->files_, message);
    if (MapFileExists(message->name_.data())) return;
    auto* request = NewCounted<FileRequestMessage>();
    AssignRef(&request->FileRef(), message);
    Append(self->fileRequests_, request);
}
void ClientOnFileGone(ClientSession* self, Message* message) {
    RemoveRaw(self->files_, message);
    DropRequestsFor(self, message);
}
// A chunk of a shared file. When chunks_ then holds as many chunks of the file as the file message announces,
// they are sorted by index, their 64 bytes each joined in a growing buffer cut to the file's size, the game
// flow writes the buffer as the map file of that name, and the requests for the file leave fileRequests_. A
// file without a name is only collected. Rebirth's MQClient returns after collecting every chunk: no file from
// the server is written.
void ClientOnFileChunk(ClientSession* self, FileChunkMessage* message) {
    Append(self->chunks_, message);
    auto* file = message->file_.Get();
    const char* none = game::kEmptyText;
    const std::size_t noneLength = std::strlen(none);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::MQClient)) return;
    if (file->name_.size() == noneLength && std::memcmp(file->name_.data(), none, noneLength) == 0)
        return;

    std::vector<FileChunkMessage*> chunks;
    int count = 0;
    for (auto& node : self->chunks_) {
        auto* chunk = node.Get();
        if (chunk->file_.ptr != file) continue;
        ++count;
        chunks.push_back(chunk);
    }
    if (count == file->chunkCount_) {
        GrowingBuffer buffer;
        // By the chunks' index. A file's chunks have different indices, and two messages of one index would
        // carry the same chunk's data: any sort writes the same bytes.
        std::sort(chunks.begin(), chunks.end(),
                  [](const FileChunkMessage* a, const FileChunkMessage* b) { return a->index_ < b->index_; });
        for (auto& chunk : chunks) {
            const std::uint8_t* data = chunk->data_;
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
}
void ClientOnObjectGone(ClientSession* self, Message* message) { RemoveRaw(self->chunks_, message); }
// The awards (applied by the tick).
void ClientOnAward(ClientSession* self, Message* message) { Append(self->awards_, message); }
// The other players' trade offers; the one paired with ours rebuilds the party.
void ClientOnParty(ClientSession* self, Message* message) {
    Append(self->offers_, message);
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void ClientOnPartyChanged(ClientSession* self, Message* message) {
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void ClientOnPartyGone(ClientSession* self, Message* message) { RemoveRaw(self->offers_, message); }
// Party members, every channel: the description committed and the party rebuilt.
void ClientOnMember(ClientSession* self, Message*) {
    ClientCommitParty(self);
    ClientApplyParty(self);
}
// The trade: a new one empties the trade records and resets both parties.
void ClientOnTrade(ClientSession* self, TradeMessage* message) {
    AssignRef(&self->trade_, message);
    ClearTradeRecords(self);
    self->parties_[0].Get()->Clear(1, 0);
    self->parties_[1].Get()->Clear(1, 0);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade,
// the party description and the records are dropped.
void ClientOnTradeChanged(ClientSession* self, TradeMessage* message) {
    if (!message->done_ || message != self->trade_) return;
    std::uint32_t a, b;
    if (message->offer_.ptr == self->party_.ptr) {
        b = message->revision_;
        a = message->partnerRevision_;
    } else {
        b = message->partnerRevision_;
        a = message->revision_;
    }
    for (TradeRecord& record : self->tradeRecords_) {
        if (record.revision == b && record.acceptedRevision == a) {
            g_interface->OnExchangeDone(&record.parties[1], &record.parties[0], &record.parties[2]);
            break;
        }
    }
    ClearRef(&self->trade_);
    DropParty(self);
    ClearTradeRecords(self);
}
void ClientOnTradeGone(ClientSession* self, Message* message) {
    if (self->trade_ != message) return;
    ClearRef(&self->trade_);
    DropParty(self);
    ClearTradeRecords(self);
}

} // namespace net
