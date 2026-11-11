// The game server's trades between players: offers arrive as replicated objects (created, updated, removed),
// two matching offers make a trade (a TradeMessage, finds it), and a removed file request is dropped a
// removed file request. The offers are PartyMessages (net_messages.h). See docs/notes/network.md.
#include "engine/core/core_refcount.h"

#include "engine/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/net/net_connection.h"
#include "game/net/net_messages.h"
#include "game/world/quest_records.h"
#include "game/ui/ui.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace net {

namespace {

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

// Offer created: lists the offer; when the partner's offer is listed, a trade of the two
// is made (both marked paired), else the partner is told of the offer.
void ServerSession::OnTradeOffer(PartyMessage* message) {
    offers.emplace_back(message);

    PartyMessage* other = FindPartnerOffer(offers, message);
    if (!other) {
        TellPartner(this, message, 10);
        return;
    }
    auto* trade = new TradeMessage();
    trade->offer.Assign(message);
    trade->partnerOffer.Assign(other);
    trade->player.Assign(message->player.Get());
    trade->partner.Assign(message->partner.Get());
    trades.emplace_back(trade);
    message->trading = 1;
    other->trading = 1;
}

// Offer updated: the offer's version moves on; when both offers of its trade are live, accepted and agree
// crosswise (each one accepted the other's current revision), the trade is done with the two revisions.
void ServerSession::OnTradeUpdate(PartyMessage* message) {
    ++message->version;
    PartyMessage* other = FindPartnerOffer(offers, message);
    TradeMessage* trade = FindTrade(message, other);
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
void ServerSession::OnTradeCancel(PartyMessage* message) {
    std::erase_if(offers, [message](const auto& offer) { return offer.Get() == message; });
    PartyMessage* other = FindPartnerOffer(offers, message);
    TradeMessage* trade = FindTrade(message, other);
    if (!message->trading) TellPartner(this, message, 11);
    if (!IsLive(trade)) return;
    if (trade->done) {
        HeldRef<TradeMessage> held(trade);
        TimedEntry<HeldRef<TradeMessage>> entry(kTimedEntryLifetime, held);
        finishedTrades.push_back(entry);
    }
    // remove(trade), the trade held meanwhile.
    HeldRef<RefObject> removing(trade);
    std::erase_if(trades, [&removing](const auto& held) { return held.Get() == removing.Get(); });
}

// --- Net server awards -------------------------------------------------------------------------------

// The game server's awards: the zone completion command ("<zone>_3"), the host's item awards and the queued
// award message, plus the counted pointer copy they use. See net/net_session.h and docs/notes/network.md.

// Queues an award message for `connection` on the pending awards with a 60 second delay. `text` is the
// caller's string (made in the argument slot), destroyed here as the callee destroys a by-value argument.
void ServerSession::QueueAward(RefObject* connection, AwardKind awardKind, float value, std::string text) {
    auto* award = new AwardMessage();
    award->connection.Assign(connection);
    award->kind = awardKind;
    award->value = value;

    award->text = text;

    // Shared three times over: the list keeps a clone of the entry's clone (each share marks the object
    // copy-on-share); the entry's and then the first share are destroyed.
    SharedRef<AwardMessage> shared(award);
    TimedEntry<SharedRef<AwardMessage>> entry(kTimedEntryLifetime, shared);
    awards.push_back(entry);
}

// Finds the host (the first binding whose player is live and has the host flag) and,
// when its connection is live, queues an Item award for each item name of `entry` (its given items)
// and, with `withFinal`, a QuestReset award with an empty text.
void ServerSession::AwardHost(const QuestBriefingRecord* entry, bool withFinal) {
    auto host = std::find_if(bindings.begin(), bindings.end(), [](const auto& binding) {
        auto* player = binding.Get()->player.Get();
        return IsLive(player) && player->host;
    });
    if (host == bindings.end()) return;
    RefObject* connection = host->Get()->connection.Get();
    if (!IsLive(connection)) return;

    for (const std::string& item : entry->lists[0]) QueueAward(connection, AwardKind::Item, 0.0f, item);   // the items given
    if (withFinal) QueueAward(connection, AwardKind::QuestReset, 0.0f, std::string());
}

// The zone completion command ("<zone>_3"). Announces the current map's title (text 7)
// when a map message is held, then, when the map registry knows "zone_3", queues for every client
// its share of the entry's two values (exp as Experience, money as Gold, divided by the number of
// clients, at least 1) and the host's items with the final award. Then drops the map message and
// empties queuedMaps, variables and files, and queues the following zones
void ServerSession::CompleteZone(const std::string& zone) {
    auto* holder = &map;
    if (IsLive(holder->Get())) {
        Announce(g_interface.MessageText(7, SessionMapName()), nullptr);
    }

    QuestBriefingRecord* entry = g_questDatabase.briefings.index[zone + kZoneCompleteSuffix];
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
            QueueAward(connection, AwardKind::Experience, award0, std::string());
            QueueAward(connection, AwardKind::Gold, award1, std::string());
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
        Announce(g_interface.MessageText(6, SessionMapName()), nullptr);

        QuestBriefingRecord* entry = g_questDatabase.briefings.index[holder->Get()->text + kZoneEnterSuffix];
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
