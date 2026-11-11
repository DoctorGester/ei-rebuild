// The game client's message handlers (OnMessage) and the party-screen entry points around them. Each keeps the replicated objects the server sends in one of the client's lists and tells the
// interface or the game flow. The messages are declared in net_messages.h. See docs/notes/network.md.
#include "game/core/core_refcount.h"
#include "game/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/ui/ui.h"
#include "game/mods/maps_folder.h"

#include "lib/crt.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

namespace net {

namespace {

// Every element holding `value` erased.
template <typename List>
void RemoveValue(List& list, const void* value) {
    std::erase_if(list, [value](const auto& element) { return element.Get() == value; });
}
// The message appended (the list's element takes its own reference or share).
template <typename List, typename T>
void Append(List& list, T* message) {
    list.emplace_back(message);
}
template <typename List>
void RemoveRaw(List& list, Message* message) {
    RemoveValue(list, message);
}

template <typename List>
void RemoveCounted(List& list, Message* message) {
    HeldRef<RefObject> held(message);
    RemoveValue(list, held.Get());
}

// Every request for `file` erased.
void DropRequestsFor(ClientSession* self, const Message* file) {
    std::erase_if(self->fileRequests, [file](const auto& request) { return request.Get()->file.Get() == file; });
}

} // namespace

// --- The party-screen entry points -------------------------------------------------------------

// Records the trade the paired offer stands for: our offer accepts the other's current
// revision (and changes), and unless a record of the two revisions exists one is added with copies
// of the two parties (parties) and `party`.
void ClientSession::RecordTrade(Party* tradeParty) {
    PartyMessage* remote = FindParty();
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
    record.parties[0].Assign(&parties[0]);
    record.parties[1].Assign(&parties[1]);
    record.parties[2].Assign(tradeParty);
}

// Starts our trade offer to `partner` when the binding and its player are
// live: kept, from the binding's player.
void ClientSession::StartParty(PlayerMessage* partner) {
    BindingMessage* currentBinding = binding;
    if (!IsLive(currentBinding)) return;
    if (!IsLive(currentBinding->player.Get())) return;
    ShareAssignOwned(party, new PartyMessage());
    party->player.Assign(binding->player.Get());
    party->partner.Assign(partner);
}

// The server's messages. Players and variables refresh the interface's player list (1 for a new or removed
// player, 0 for a change) and the game flow on every event; party offers and their members rebuild the partner's
// party when the offer is paired with ours.
void ClientSession::OnMessage(Connection*, MessageEvent event, Message* message) {
    using enum MessageEvent;
    switch (message->Type()) {
    case BindingMessage::kType:
        if (event == Created) OnBinding(static_cast<BindingMessage*>(message));
        if (event == Removed) binding.Reset();
        return;
    case PlayerMessage::kType:
        if (event == Created) Append(players, static_cast<PlayerMessage*>(message));
        if (event == Removed) RemoveCounted(players, message);
        g_interface.OnPlayersChanged(event == Changed ? 0 : 1);
        return;
    case LineMessage::kType:
        // Each new line is shown with its number.
        if (event == Created) {
            auto* line = static_cast<LineMessage*>(message);
            Append(lines, line);
            g_interface.OnChatLine(line->text);
        }
        if (event == Removed) RemoveCounted(lines, message);
        return;
    case HostMessage::kType:
        if (event == Created) info.Assign(static_cast<HostMessage*>(message));
        if (event == Removed) info.Reset();
        return;
    case ZoneMessage::kType: {
        // The zones offered, passed to the game flow.
        auto* zone = static_cast<ZoneMessage*>(message);
        if (event == Created) {
            Append(zones, zone);
            g_gameFlow.MarkBriefing(zone->text.data());
        }
        if (event == Removed) {
            RemoveCounted(zones, zone);
            g_gameFlow.ForgetBriefing(zone->text.data());
        }
        return;
    }
    case MapMessage::kType:
        if (event == Created) OnMap(static_cast<MapMessage*>(message));
        if (event == Removed) map.Reset();
        return;
    case VariableMessage::kType:
        if (event == Created) Append(variables, static_cast<VariableMessage*>(message));
        if (event == Removed) RemoveRaw(variables, message);
        g_gameFlow.UpdateFromServer();
        return;
    case FileMessage::kType:
        if (event == Created) OnFile(static_cast<FileMessage*>(message));
        if (event == Removed) {
            RemoveRaw(files, message);
            DropRequestsFor(this, message);
        }
        return;
    case FileChunkMessage::kType:
        if (event == Created) OnFileChunk(static_cast<FileChunkMessage*>(message));
        if (event == Removed) RemoveRaw(chunks, message);
        return;
    case AwardMessage::kType:
        // Applied by the tick.
        if (event == Created) Append(awards, static_cast<AwardMessage*>(message));
        return;
    case PartyMessage::kType:
        // The other players' trade offers.
        if (event == Removed) {
            RemoveRaw(offers, message);
            return;
        }
        if (event == Created) Append(offers, static_cast<PartyMessage*>(message));
        if (FindParty() != message) return;
        CommitParty();
        ApplyParty();
        return;
    case PartyMemberMessage::kType:
        CommitParty();
        ApplyParty();
        return;
    case TradeMessage::kType:
        if (event == Created) OnTrade(static_cast<TradeMessage*>(message));
        if (event == Changed) OnTradeChanged(static_cast<TradeMessage*>(message));
        if (event == Removed && trade == message) EndTrade();
        return;
    default:
        return;   // the client's own messages
    }
}

// Our binding. An update lists its objects in the order the server tracked them (Connection::SendUpdate), so the
// players may come before the binding; their refresh then listed nobody (ShowPlayers lists only with the binding)
// and the original's binding does not refresh: the base screen's faces followed the heap. Ours refreshes the list
// when players came before the binding, as if it came first.
void ClientSession::OnBinding(BindingMessage* message) {
    binding.Assign(message);
    if (!players.empty()) g_interface.OnPlayersChanged(1);
}
// The current map; without a server of our own the flow loads it.
void ClientSession::OnMap(MapMessage* message) {
    map.Assign(message);
    ServerSession* server = g_gameServer;
    if (IsLive(server)) return;
    LoadZoneQuests(message->text.data());
    g_gameFlow.RegisterQuests(message->text.data());
}
// A shared file; for a file the flow lacks, a request holding the file goes on fileRequests.
void ClientSession::OnFile(FileMessage* message) {
    Append(files, message);
    if (MapFileExists(message->name.data())) return;
    auto* request = new FileRequestMessage();
    request->file.Assign(message);
    Append(fileRequests, request);
}
// A chunk of a shared file: collected, never written (Rebirth's MQClient, always on: a client writes no file the
// server sends; the original joined a file's chunks once all had come and wrote them to the maps folder).
void ClientSession::OnFileChunk(FileChunkMessage* message) { Append(chunks, message); }
// A new trade empties the trade records and resets both parties.
void ClientSession::OnTrade(TradeMessage* message) {
    trade.Assign(message);
    tradeRecords.clear();
    parties[0].Clear(true, false);
    parties[1].Clear(true, false);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade ends.
void ClientSession::OnTradeChanged(TradeMessage* message) {
    if (!message->done || message != trade) return;
    std::uint32_t a, b;
    if (message->offer.Get() == party.Get()) {
        b = message->revision;
        a = message->partnerRevision;
    } else {
        b = message->partnerRevision;
        a = message->revision;
    }
    for (TradeRecord& record : tradeRecords) {
        if (record.revision == b && record.acceptedRevision == a) {
            g_interface.OnExchangeDone(&record.parties[1], &record.parties[0], &record.parties[2]);
            break;
        }
    }
    EndTrade();
}
// The trade, the party description and the trade records dropped.
void ClientSession::EndTrade() {
    trade.Reset();
    DropParty();
    tradeRecords.clear();
}

} // namespace net
