// The game server's trades between players: offers arrive as replicated objects (created, updated, removed),
// two matching offers make a trade (a TradeMessage, finds it), and a removed file request is dropped a
// removed file request. The offers are PartyMessages (net_messages.h). See docs/notes/network.md.
#include "core_refcount.h"
#include "game_singletons.h"
#include "net_messages.h"
#include "ui.h"
#include "world_data.h"

#include <cstring>
#include <string>

namespace net {

namespace {


constexpr std::uint32_t kFinishedDelay = 0x42700000;   // 60.0f

// The offer of `offer`'s partner (its player is our partner and its partner our player), or null.
PartyMessage* FindPartnerOffer(const std::list<HeldRef<PartyMessage>>& list, PartyMessage* offer) {
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
    for (auto node = self->fileRequests.begin(); node != self->fileRequests.end();) {
        auto next = std::next(node);
        if (node->Get() == message) self->fileRequests.erase(node);
        node = next;
    }
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
    for (auto node = self->offers.begin(); node != self->offers.end();) {
        auto next = std::next(node);
        if (node->Get() == message) self->offers.erase(node);
        node = next;
    }
    PartyMessage* other = FindPartnerOffer(self->offers, message);
    TradeMessage* trade = self->FindTrade(message, other);
    if (!message->trading) TellPartner(self, message, 11);
    if (!IsLive(trade)) return;
    if (trade->done) {
        HeldRef<RefObject> held(trade);
        TimedEntry<HeldRef<RefObject>> entry(kFinishedDelay, held);
        self->finishedTrades.push_back(entry);
    }
    // remove(trade): each element holding it erased as the walk reaches it, the trade held meanwhile.
    HeldRef<RefObject> removing(trade);
    for (auto node = self->trades.begin(); node != self->trades.end();) {
        auto next = std::next(node);
        if (node->Get() == removing.Get()) self->trades.erase(node);
        node = next;
    }
}

} // namespace net
