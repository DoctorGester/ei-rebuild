// The game client's message handlers (registered by the constructor) and the three party-screen entry points
// around them. Each keeps the replicated objects the server sends in one of the client's lists and tells the
// interface or the game flow. The messages are declared in net_messages.h. See docs/notes/network.md.
#include "core_refcount.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_messages.h"
#include "party.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "world_map_files.h"

#include "lib/crt.h"

#include "ei/rebirth.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

namespace net {

void ClientApplyParty(ClientSession* self);      // net_client_party.cpp

namespace {

// Every node holding `value` erased (the sentinel read once, the next node taken first).
template <typename List>
void RemoveValue(List& list, const void* value) {
    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        if (node->Get() == value) list.erase(node);
        node = next;
    }
}
// The message appended (the list's element takes its own reference or share).
template <typename List>
void Append(List& list, Message* message) {
    using Element = std::remove_pointer_t<decltype(list.front().Get())>;
    list.emplace_back(static_cast<Element*>(message));
}
template <typename List>
void RemoveRaw(List& list, Message* message) {
    RemoveValue(list, message);
}

template <typename List>
void RemoveCounted(List& list, Message* message) {
    HeldRef<CRefObject> held(message);
    RemoveValue(list, held.Get());
}

// Every request for `file` erased (the sentinel read once, the next node taken first).
void DropRequestsFor(ClientSession* self, const Message* file) {
    for (auto node = self->fileRequests.begin(); node != self->fileRequests.end();) {
        auto next = std::next(node);
        if (node->Get()->FileRef().Get() == file) self->fileRequests.erase(node);
        node = next;
    }
}

// The owner's release of the party description as the handlers inline it, then null.
void DropParty(ClientSession* self) {
    DestroyOwned(self->party);
}

// The trade records emptied.
void ClearTradeRecords(ClientSession* self) { self->tradeRecords.clear(); }

} // namespace

// --- The party-screen entry points -------------------------------------------------------------

// Records the trade the paired offer stands for: our offer accepts the other's current
// revision (and changes), and unless a record of the two revisions exists one is added with copies
// of the two parties (parties) and `party`.
void ClientSession::RecordTrade(const void* tradeParty) {
    PartyMessage* remote = ClientFindParty(this);
    if (!remote) return;
    if (!party.Get()) return;
    auto* ours = party.Get();
    ours->accepted = 1;
    ours->acceptedRevision = remote->revision;
    ++ours->version;
    std::uint32_t a = ours->acceptedRevision;
    std::uint32_t b = ours->revision;
    for (const TradeRecord& record : tradeRecords)
        if (record.revision == b && record.acceptedRevision == a) return;
    {
        // insert(end()): a blank record copied into the list, then the blank destroyed.
        const TradeRecord blank;
        tradeRecords.push_back(blank);
    }
    TradeRecord& record = tradeRecords.back();
    record.acceptedRevision = ours->acceptedRevision;
    record.revision = ours->revision;
    record.parties[0].Assign(parties[0].Get());
    record.parties[1].Assign(parties[1].Get());
    record.parties[2].Assign(static_cast<CParty*>(const_cast<void*>(tradeParty)));
}

// Starts our trade offer to `partner` when the binding and its player are
// live: kept, from the binding's player.
void ClientSession::StartParty(CRefObject* partner) {
    BindingMessage* currentBinding = binding;
    if (!IsLive(currentBinding)) return;
    if (!IsLive(currentBinding->PlayerRef().Get())) return;
    ShareAssignOwned(party, NewCounted<PartyMessage>());
    party->player.Assign(static_cast<PlayerMessage*>(binding->PlayerRef().Get()));
    party->partner.Assign(static_cast<PlayerMessage*>(partner));
}

// Drops the party description.
void ClientDropParty(ClientSession* self) { DropParty(self); }

// The binding of this client: kept, dropped. An update lists its
// objects in the order of the server's entries' addresses (Connection::SendUpdate), so the players may
// come before the binding; their refresh then listed nobody (ShowPlayers lists only with the binding)
// and the original's binding does not refresh: the base screen's faces followed the heap. Ours
// (address_order.h) refreshes the list when players came before the binding, as if it came first.
void ClientOnBinding(ClientSession* self, BindingMessage* message) {
    self->binding.Assign(message);
    if (!self->players.empty()) g_interface.OnPlayersChanged(1);
}
void ClientOnBindingGone(ClientSession* self, Message*) { self->binding.Reset(); }
// The players, with the interface's player list refreshed.
void ClientOnPlayer(ClientSession* self, Message* message) {
    Append(self->players, message);
    g_interface.OnPlayersChanged(1);
}
void ClientOnPlayerChanged(ClientSession*, Message*) { g_interface.OnPlayersChanged(0); }
void ClientOnPlayerGone(ClientSession* self, Message* message) {
    RemoveCounted(self->players, message);
    g_interface.OnPlayersChanged(1);
}
// The chat lines, each shown with its number.
void ClientOnLine(ClientSession* self, LineMessage* message) {
    Append(self->lines, message);
    g_interface.OnChatLine(message->text.data());
}
void ClientOnLineGone(ClientSession* self, Message* message) { RemoveCounted(self->lines, message); }
// The host message kept.
void ClientOnInfo(ClientSession* self, HostMessage* message) { self->info.Assign(message); }
void ClientOnInfoGone(ClientSession* self, Message*) { self->info.Reset(); }
// The zones offered, passed to the game flow.
void ClientOnZone(ClientSession* self, ZoneMessage* message) {
    Append(self->zones, message);
    g_gameFlow.MarkBriefing(message->text.data());
}
void ClientOnZoneGone(ClientSession* self, ZoneMessage* message) {
    RemoveCounted(self->zones, message);
    g_gameFlow.ForgetBriefing(message->text.data());
}
// The current map; without a server of our own the flow loads it.
void ClientOnMap(ClientSession* self, MapMessage* message) {
    self->map.Assign(message);
    ServerSession* server = g_gameServer;
    if (server && !(server->flags7 & CRefObject::kDetached)) return;
    LoadZoneQuests(message->text.data());
    g_gameFlow.RegisterQuests(message->text.data());
}
void ClientOnMapGone(ClientSession* self, Message*) { self->map.Reset(); }
// The script variables; the flow is told of every change.
void ClientOnVariable(ClientSession* self, Message* message) {
    Append(self->variables, message);
    g_gameFlow.UpdateFromServer();
}
void ClientOnVariableChanged(ClientSession*, Message*) { g_gameFlow.UpdateFromServer(); }
void ClientOnVariableGone(ClientSession* self, Message* message) {
    RemoveRaw(self->variables, message);
    g_gameFlow.UpdateFromServer();
}
// The shared files; for a file the flow lacks, a request holding the file goes on fileRequests.
void ClientOnFile(ClientSession* self, FileMessage* message) {
    Append(self->files, message);
    if (MapFileExists(message->name.data())) return;
    auto* request = NewCounted<FileRequestMessage>();
    request->FileRef().Assign(message);
    Append(self->fileRequests, request);
}
void ClientOnFileGone(ClientSession* self, Message* message) {
    RemoveRaw(self->files, message);
    DropRequestsFor(self, message);
}
// A chunk of a shared file. When chunks then holds as many chunks of the file as the file message announces,
// they are sorted by index, their 64 bytes each joined in a growing buffer cut to the file's size, the game
// flow writes the buffer as the map file of that name, and the requests for the file leave fileRequests. A
// file without a name is only collected. Rebirth's MQClient returns after collecting every chunk: no file from
// the server is written.
void ClientOnFileChunk(ClientSession* self, FileChunkMessage* message) {
    Append(self->chunks, message);
    auto* file = message->file.Get();
    const char* none = game::kEmptyText;
    const std::size_t noneLength = std::strlen(none);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::MQClient)) return;
    if (file->name.size() == noneLength && std::memcmp(file->name.data(), none, noneLength) == 0)
        return;

    std::vector<FileChunkMessage*> chunks;
    int count = 0;
    for (auto& node : self->chunks) {
        auto* chunk = node.Get();
        if (chunk->file.Get() != file) continue;
        ++count;
        chunks.push_back(chunk);
    }
    if (count == file->chunkCount) {
        GrowingBuffer buffer;
        // By the chunks' index. A file's chunks have different indices, and two messages of one index would
        // carry the same chunk's data: any sort writes the same bytes.
        std::sort(chunks.begin(), chunks.end(),
                  [](const FileChunkMessage* a, const FileChunkMessage* b) { return a->index < b->index; });
        for (auto& chunk : chunks) {
            const std::uint8_t* data = chunk->data;
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
        const std::int32_t size = file->size;
        buffer.high = buffer.begin + size;
        if (buffer.high > buffer.end) buffer.Grow(static_cast<std::uint32_t>(size));
        WriteMapFile(file->name.data(), &buffer);
        DropRequestsFor(self, file);
    }
}
void ClientOnObjectGone(ClientSession* self, Message* message) { RemoveRaw(self->chunks, message); }
// The awards (applied by the tick).
void ClientOnAward(ClientSession* self, Message* message) { Append(self->awards, message); }
// The other players' trade offers; the one paired with ours rebuilds the party.
void ClientOnParty(ClientSession* self, Message* message) {
    Append(self->offers, message);
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void ClientOnPartyChanged(ClientSession* self, Message* message) {
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void ClientOnPartyGone(ClientSession* self, Message* message) { RemoveRaw(self->offers, message); }
// Party members, every channel: the description committed and the party rebuilt.
void ClientOnMember(ClientSession* self, Message*) {
    ClientCommitParty(self);
    ClientApplyParty(self);
}
// The trade: a new one empties the trade records and resets both parties.
void ClientOnTrade(ClientSession* self, TradeMessage* message) {
    self->trade.Assign(message);
    ClearTradeRecords(self);
    self->parties[0].Get()->Clear(1, 0);
    self->parties[1].Get()->Clear(1, 0);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade,
// the party description and the records are dropped.
void ClientOnTradeChanged(ClientSession* self, TradeMessage* message) {
    if (!message->done || message != self->trade) return;
    std::uint32_t a, b;
    if (message->offer.Get() == self->party.Get()) {
        b = message->revision;
        a = message->partnerRevision;
    } else {
        b = message->partnerRevision;
        a = message->revision;
    }
    for (TradeRecord& record : self->tradeRecords) {
        if (record.revision == b && record.acceptedRevision == a) {
            g_interface.OnExchangeDone(&record.parties[1], &record.parties[0], &record.parties[2]);
            break;
        }
    }
    self->trade.Reset();
    DropParty(self);
    ClearTradeRecords(self);
}
void ClientOnTradeGone(ClientSession* self, Message* message) {
    if (self->trade != message) return;
    self->trade.Reset();
    DropParty(self);
    ClearTradeRecords(self);
}

} // namespace net
