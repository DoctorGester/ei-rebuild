// The game server's trades between players: offers arrive as replicated objects (created, updated, removed),
// two matching offers make a trade (a TradeMessage, finds it), and a removed file request is dropped a
// removed file request. The offers are PartyMessages (net_messages.h). See docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ui.h"
#include "world_data.h"
#include "game_singletons.h"

#include <cstring>
#include <string>

namespace net {

namespace {


constexpr std::uint32_t kFinishedDelay = 0x42700000;   // 60.0f

// The offer of `offer`'s partner (its player is our partner and its partner our player), or null.
PartyMessage* FindPartnerOffer(const std::list<CountedRefTo<PartyMessage>>& list, PartyMessage* offer) {
    for (auto& node : list) {
        auto* other = node.Get();
        if (other->player_.ptr == offer->partner_.ptr && other->partner_.ptr == offer->player_.ptr)
            return node.Get();
    }
    return nullptr;
}

// The partner is told a trade was offered (text 10) or withdrawn (text 11), with the player's name.
void TellPartner(ServerSession* self, PartyMessage* offer, int text) {
    CRefObject* partner = offer->partner_.ptr;
    const char* name = offer->player_.Get()->name_.data();
    std::string line;
    std::string* formatted = g_interface->MessageText(&line, text, name);
    self->Announce(*formatted, partner);
}

} // namespace

} // namespace net

namespace net {

// The trade between the two offers (in either order), or null.
TradeMessage* ServerSession::FindTrade(const PartyMessage* offer, const PartyMessage* other) {
    for (auto& node : trades_) {
        TradeMessage* trade = node.Get();
        const CRefObject* first = trade->offer_.ptr;
        if ((first == offer && trade->partnerOffer_.ptr == other) || (first == other && trade->partnerOffer_.ptr == offer))
            return trade;
    }
    return nullptr;
}

// Removed file request: drops `message`.
void ServerOnObjectRemoved(ServerSession* self, Message* message) {
    for (auto node = self->fileRequests_.begin(); node != self->fileRequests_.end();) {
        auto next = std::next(node);
        if (node->ptr == message) self->fileRequests_.erase(node);
        node = next;
    }
}

// Offer created: lists the offer; when the partner's offer is listed, a trade of the two
// is made (both marked paired), else the partner is told of the offer.
void ServerOnTradeOffer(ServerSession* self, PartyMessage* message) {
    self->offers_.emplace_back(message);

    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    if (!other) {
        TellPartner(self, message, 10);
        return;
    }
    auto* trade = NewCounted<TradeMessage>();
    trade->offer_.Assign(message);
    trade->partnerOffer_.Assign(other);
    trade->player_.Assign(message->player_.ptr);
    trade->partner_.Assign(message->partner_.ptr);
    self->trades_.emplace_back(trade);
    message->trading_ = 1;
    other->trading_ = 1;
}

// Offer updated: the offer's version moves on; when both offers of its trade are live, accepted and agree
// crosswise (each one accepted the other's current revision), the trade is done with the two revisions.
void ServerOnTradeUpdate(ServerSession* self, PartyMessage* message) {
    ++message->version_;
    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    TradeMessage* trade = self->FindTrade(message, other);
    if (!IsLive(trade)) return;
    auto* first = trade->offer_.Get();
    if (!IsLive(first)) return;
    auto* second = trade->partnerOffer_.Get();
    if (!IsLive(second)) return;
    if (!first->accepted_ || first->acceptedRevision_ != second->revision_) return;
    if (!second->accepted_ || second->acceptedRevision_ != first->revision_) return;
    trade->done_ = 1;
    trade->revision_ = first->revision_;
    trade->partnerRevision_ = second->revision_;
    ++trade->version_;
}

// Offer removed: drops the offer; an unpaired offer's partner is told it was withdrawn.
// Its trade, when live, is dropped, a done one first kept on finishedTrades_ for 60 seconds.
void ServerOnTradeCancel(ServerSession* self, PartyMessage* message) {
    for (auto node = self->offers_.begin(); node != self->offers_.end();) {
        auto next = std::next(node);
        if (node->ptr == message) self->offers_.erase(node);
        node = next;
    }
    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    TradeMessage* trade = self->FindTrade(message, other);
    if (!message->trading_) TellPartner(self, message, 11);
    if (!IsLive(trade)) return;
    if (trade->done_) {
        CountedRefPtr held(trade);
        TimedEntry<CountedRefPtr> entry(kFinishedDelay, held);
        self->finishedTrades_.push_back(entry);
    }
    // remove(trade): each element holding it erased as the walk reaches it, the trade held meanwhile.
    CountedRefPtr removing(trade);
    for (auto node = self->trades_.begin(); node != self->trades_.end();) {
        auto next = std::next(node);
        if (node->ptr == removing.ptr) self->trades_.erase(node);
        node = next;
    }
}

} // namespace net
