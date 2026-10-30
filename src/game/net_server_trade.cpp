// The game server's trades between players: offers arrive as replicated objects (created, updated, removed),
// two matching offers make a trade (a TradeMessage, finds it), and a removed file request is dropped a
// removed file request. The offers are PartyMessages (net_messages.h). See docs/notes/network.md.
#include "core_refcount.h"

#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_connection.h"
#include "net_messages.h"
#include "quest_records.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "lib/crt.h"

#include <cstring>
#include <string>
#include <vector>

namespace net {

namespace {

constexpr std::uint32_t kFinishedDelay = 0x42700000;   // 60.0f

// The offer of `offer`'s partner (its player is our partner and its partner our player), or null.
PartyMessage* FindPartnerOffer(const std::vector<HeldRef<PartyMessage>>& list, PartyMessage* offer) {
    for (auto& node : list) {
        auto* other = node.Get();
        if (other->player.Get() == offer->partner.Get() && other->partner.Get() == offer->player.Get()) return node.Get();
    }
    return nullptr;
}

// The partner is told a trade was offered (text 10) or withdrawn (text 11), with the player's name.
void TellPartner(ServerSession* self, PartyMessage* offer, int text) {
    RefObject* partner = offer->partner.Get();
    const char* name = offer->player.Get()->name.data();
    self->Announce(g_interface.MessageText(text, name), partner);
}

} // namespace

} // namespace net

namespace net {

// The trade between the two offers (in either order), or null.
TradeMessage* ServerSession::FindTrade(const PartyMessage* offer, const PartyMessage* other) {
    for (auto& node : trades) {
        TradeMessage* trade = node.Get();
        const RefObject* first = trade->offer.Get();
        if ((first == offer && trade->partnerOffer.Get() == other) || (first == other && trade->partnerOffer.Get() == offer))
            return trade;
    }
    return nullptr;
}

// Removed file request: drops `message`.
void ServerOnObjectRemoved(ServerSession* self, Message* message) {
    std::erase_if(self->fileRequests, [message](const auto& request) { return request.Get() == message; });
}

// Offer created: lists the offer; when the partner's offer is listed, a trade of the two
// is made (both marked paired), else the partner is told of the offer.
void ServerOnTradeOffer(ServerSession* self, PartyMessage* message) {
    self->offers.emplace_back(message);

    PartyMessage* other = FindPartnerOffer(self->offers, message);
    if (!other) {
        TellPartner(self, message, 10);
        return;
    }
    auto* trade = NewCounted<TradeMessage>();
    trade->offer.Assign(message);
    trade->partnerOffer.Assign(other);
    trade->player.Assign(message->player.Get());
    trade->partner.Assign(message->partner.Get());
    self->trades.emplace_back(trade);
    message->trading = 1;
    other->trading = 1;
}

// Offer updated: the offer's version moves on; when both offers of its trade are live, accepted and agree
// crosswise (each one accepted the other's current revision), the trade is done with the two revisions.
void ServerOnTradeUpdate(ServerSession* self, PartyMessage* message) {
    ++message->version;
    PartyMessage* other = FindPartnerOffer(self->offers, message);
    TradeMessage* trade = self->FindTrade(message, other);
    if (!IsLive(trade)) return;
    auto* first = trade->offer.Get();
    if (!IsLive(first)) return;
    auto* second = trade->partnerOffer.Get();
    if (!IsLive(second)) return;
    if (!first->accepted || first->acceptedRevision != second->revision) return;
    if (!second->accepted || second->acceptedRevision != first->revision) return;
    trade->done = 1;
    trade->revision = first->revision;
    trade->partnerRevision = second->revision;
    ++trade->version;
}

// Offer removed: drops the offer; an unpaired offer's partner is told it was withdrawn.
// Its trade, when live, is dropped, a done one first kept on finishedTrades for 60 seconds.
void ServerOnTradeCancel(ServerSession* self, PartyMessage* message) {
    std::erase_if(self->offers, [message](const auto& offer) { return offer.Get() == message; });
    PartyMessage* other = FindPartnerOffer(self->offers, message);
    TradeMessage* trade = self->FindTrade(message, other);
    if (!message->trading) TellPartner(self, message, 11);
    if (!IsLive(trade)) return;
    if (trade->done) {
        HeldRef<RefObject> held(trade);
        TimedEntry<HeldRef<RefObject>> entry(kFinishedDelay, held);
        self->finishedTrades.push_back(entry);
    }
    // remove(trade), the trade held meanwhile.
    HeldRef<RefObject> removing(trade);
    std::erase_if(self->trades, [&removing](const auto& held) { return held.Get() == removing.Get(); });
}

} // namespace net

// --- Net server awards -------------------------------------------------------------------------------

// The game server's awards: the zone completion command ("<zone>_3"), the host's item awards and the queued
// award message, plus the counted pointer copy they use. See net_classes.h and docs/notes/network.md.

namespace net {

namespace {

constexpr std::uint32_t kAwardDelay = 0x42700000;   // 60.0f

} // namespace

} // namespace net

namespace net {

// Queues an award message for `connection` on the pending awards with a 60 second delay. `text` is the
// caller's string (made in the argument slot), destroyed here as the callee destroys a by-value argument.
void ServerSession::QueueAward(RefObject* connection, int textKind, float value, std::string text) {
    auto* award = NewCounted<AwardMessage>();
    award->connection.Assign(connection);
    award->kind = static_cast<std::uint32_t>(textKind);
    award->value = value;

    // The text, assigned in place (string::operator= inlined: overwrite, then cut or append).
    award->text = text;

    // Shared three times over: the list keeps a clone of the entry's clone (each share marks the object
    // copy-on-share); the entry's and then the first share are destroyed.
    SharedRef<RefObject> shared(award);
    TimedEntry<SharedRef<RefObject>> entry(kAwardDelay, shared);
    awards.push_back(entry);
}

// Finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues a kind 2 award for each item name of `entry` (its given items)
// and, with `withFinal`, a kind 4 award with an empty text.
void ServerSession::AwardHost(const QuestBriefingRecord* entry, bool withFinal) {
    auto sentinel = bindings.end();
    auto node = bindings.begin();
    for (;;) {
        if (node == sentinel) return;
        auto* binding = node->Get();
        auto* player = binding->PlayerInfo();
        if (IsLive(player) && player->host) break;
        ++node;
    }
    RefObject* connection = node->Get()->ConnectionRef().Get();
    if (!IsLive(connection)) return;

    const std::vector<std::string>& items = entry->lists[0];   // the items given
    auto count = [&items] { return static_cast<std::uint32_t>(items.end() - items.begin()); };
    if (count() != 0) {
        std::uint32_t i = 0;
        if (count() != 0) {
            do {
                QueueAward(connection, 2, 0.0f, items.begin()[i]);
                ++i;
            } while (i < count());
        }
    }
    if (withFinal) QueueAward(connection, 4, 0.0f, std::string(game::kEmptyText));
}

// The zone completion command ("<zone>_3"). Announces the current map's title (text 7)
// when a map message is held, then, when the map registry knows "zone_3", queues for every client
// its share of the entry's two values (exp as kind 0, money as kind 1, divided by the number of
// clients, at least 1) and the host's items with the final award. Then drops the map message and
// empties queuedMaps, variables and files, and queues the following zones
void ServerSession::CompleteZone(const std::string* zone) {
    auto* holder = &map;
    if (IsLive(holder->Get())) {
        Announce(g_interface.MessageText(7, SessionMapName().c_str()), nullptr);
    }

    QuestBriefingRecord* entry;
    {
        std::string key = *zone + game::ZoneSuffix3Text();
        entry = worlddata::QuestBriefings()[key];
    }
    if (entry) {
        int count = static_cast<int>(clients.size());
        int share = count < 1 ? 1 : count;
        const float value0 = entry->exp;
        const float value1 = entry->money;
        const auto divisor = static_cast<float>(share);
        const float award0 = value0 / divisor;
        const float award1 = value1 / divisor;

        for (auto& node : clients) {
            RefObject* connection = node.Get();
            const char* empty = game::kEmptyText;
            QueueAward(connection, 0, award0, std::string(empty));
            QueueAward(connection, 1, award1, std::string(empty));
        }
        AwardHost(entry, true);
    }

    DestroyOwned(*holder);
    queuedMaps.clear();
    variables.clear();
    files.clear();
    QueueMapList();
}

// The zone leaving command ("<zone>_2"). When a map message is held: announces the map's title (text 6) and,
// when the map registry knows "<map>_1", queues the host's items with the final award. Then drops the map
// message, empties queuedMaps, variables and files, queues the following zones and clears the
// world's map (with null).
void ServerSession::LeaveZone() {
    auto* holder = &map;
    if (IsLive(holder->Get())) {
        Announce(g_interface.MessageText(6, SessionMapName().c_str()), nullptr);

        QuestBriefingRecord* entry;
        {
            std::string key = holder->Get()->text + game::ZoneSuffix1Text();
            std::string lookup(key.data());
            entry = worlddata::QuestBriefings()[lookup];
        }
        if (entry) AwardHost(entry, true);
    }

    DestroyOwned(*holder);
    queuedMaps.clear();
    variables.clear();
    files.clear();
    QueueMapList();
    g_gameFlow.SetNetworkZone(nullptr);
}

} // namespace net
