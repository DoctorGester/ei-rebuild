// A connection's tick (00433BD0): clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "net_connection.h"

#include "ei/hook.h"
#include "lib/stlp_vector.h"
#include "ei/x87.h"
#include "net_connection_trees.h"
#include "library_net_lists.h"
#include "originals_library.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace net {

namespace {

inline double D(float f) { return f; }


// The history / acknowledged lists and the freed-id queue (library_net_lists.h).
using Packets = stlp::list<SentPacket>;
using Batches = stlp::list<FreedIds>;
using netlists::FreedIdsElement;
using netlists::PacketRecordElement;

// The clocks are doubles read and written as their 8 bytes.
double Get(const double* d) {
    double v;
    std::memcpy(&v, d, 8);
    return v;
}
void Set(double* d, double v) { std::memcpy(d, &v, 8); }

// The reliable stream's size (the buffer's high mark follows its position first).
std::int32_t ReliableSize(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

} // namespace

// 00433BD0: advances the idle time and the clock by `dt`. Packets older than the loss timeout
// (3 standard deviations above the average round trip, at least 0.1, at most the session's
// maxRoundTrip_) are rolled back and move to the acknowledged list, where they stay for
// maxRoundTrip_ seconds. Sends an update every sendInterval_ (stopping early once the reliable
// stream and the header queue are empty). Every idReuseTime_ seconds the oldest batch of freed ids
// returns to the id pools. False once the peer was silent for the session's timeout_ or the
// reliable stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    CurrentConnection() = this;
    idleTime_ = static_cast<float>(D(idleTime_) + dt);
    Set(&clock_, dt + Get(&clock_));
    float spread = static_cast<float>(D(roundTrip_) * D(roundTrip_) - D(roundTripSquare_));
    std::uint32_t bits;
    std::memcpy(&bits, &spread, 4);
    bits &= 0x7FFFFFFF;
    std::memcpy(&spread, &bits, 4);
    float loss = static_cast<float>(std::sqrt(spread) * D(3.0f /*0073BAA4*/) + D(roundTrip_));
    float tenth;
    const std::uint32_t tenthBits = 0x3DCCCCCD;   // 0.1f
    std::memcpy(&tenth, &tenthBits, 4);
    loss = loss >= tenth ? loss : tenth;
    float longest = static_cast<float>(static_cast<double>(CurrentSession()->maxRoundTrip_));
    loss = longest >= loss ? loss : longest;
    float kept = static_cast<float>(static_cast<double>(CurrentSession()->maxRoundTrip_));

    for (HistoryNode* node = history_.First(); node != history_.End();) {
        double age = Get(&clock_) - Get(&node->data.time);
        EI_NAN_AUDIT(loss, age);
        if (D(loss) >= age) break;
        RollBack(&node->data);
        HistoryNode* next = node->next;
        Packets::SpliceOne(acknowledged_.End(), node);   // 004380C0
        node = next;
        if (node == history_.End()) break;
    }
    for (HistoryNode* node = acknowledged_.First(); node != acknowledged_.End();) {
        double age = Get(&clock_) - Get(&node->data.time);
        if (D(kept) < age) {
            node = Packets::Erase<PacketRecordElement>(node);   // 00437F30
        } else {
            node = node->next;   // also NaN
        }
    }

    double timer = D(sendTimer_) + dt;
    sendTimer_ = static_cast<float>(timer);
    if (timer > D(sendInterval_)) {
        do {
            sendTimer_ = static_cast<float>(D(sendTimer_) - D(sendInterval_));
            if (SendUpdate(static_cast<int>(updateBudget_)) && headers_.Empty()) {
                if (ReliableSize(&reliableOut_) == 0) break;
            }
        } while (sendTimer_ > sendInterval_);
    }
    if (sendTimer_ > sendInterval_) {
        double rest;
        do {
            rest = D(sendTimer_) - D(sendInterval_);
            sendTimer_ = static_cast<float>(rest);
        } while (rest > D(sendInterval_));
    }

    double freedTime = D(freedTimer_) + dt;
    freedTimer_ = static_cast<float>(freedTime);
    if (freedTime > static_cast<double>(CurrentSession()->idReuseTime_)) {
        freedTimer_ = 0.0f;
        FreedIds& oldest = freedIds_.First()->data;
        for (FreedId* e = oldest.begin(); e != oldest.end(); ++e) {
            auto id = static_cast<std::uint16_t>(e->id);
            stlp::vector<std::uint16_t>& pool = idPools_[e->type].free;
            if (pool.end() != pool.EndOfStorage()) {
                stlp::ConstructAt(pool.end(), &id);   // 0043D7E0
                ++pool.Finish();
            } else {
                pool.InsertOverflow<stlp::PlainElement<std::uint16_t>>(pool.end(), id, 1);   // 0043B530
            }
        }
        oldest.Finish() = stlp::CopyElements(oldest.end(), oldest.end(), oldest.begin());   // erase(begin, end), 0043DC80
        Batches::Erase<FreedIdsElement>(freedIds_.First());   // 004390C0
        Batches::InsertDefault<FreedIdsElement>(freedIds_.End());   // 00439000
    }

    bool alive = false;
    if (static_cast<double>(CurrentSession()->timeout_) > D(idleTime_)) alive = ReliableSize(&reliableOut_) < 32000;
    CurrentConnection() = nullptr;
    return alive;
}
EI_REWRITTEN(0x00433BD0, Connection::Tick);

} // namespace net

namespace net {

namespace {
float RttKeep() { return 0.93f /*0073BF30*/; }   // the share of the old average kept per sample
float One() { return 1.0f /*0073B84C*/; }       // 1.0f
} // namespace

void Connection::Delivered(SentPacket* packet) {
    double sample = Get(&clock_) - Get(&packet->time);
    double kept = D(RttKeep()) * D(roundTrip_);
    roundTrip_ = static_cast<float>(kept);
    roundTrip_ = static_cast<float>((D(One()) - D(RttKeep())) * sample + kept);
    double keptSquare = D(RttKeep()) * D(roundTripSquare_);
    roundTripSquare_ = static_cast<float>(keptSquare);
    roundTripSquare_ = static_cast<float>((D(One()) - D(RttKeep())) * sample * sample + keptSquare);
    for (SentItemNode* node = packet->items.First(); node != packet->items.End(); node = node->next) {
        SentItem* item = &node->data;
        auto action = static_cast<std::int32_t>(item->action);
        if (action == SentItem::kChangedReliable) {
            std::uint32_t version = item->version;
            if (item->entry->version_ > version) version = item->entry->version_;
            item->entry->version_ = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            EntryMap* tree = &priorities_[entry->flags_ >> 1];
            EntryMap::Node* found;
            trees::Entries::Find(tree, nullptr, &found, &entry);
            if (found != tree->end()) {
                entry = item->entry;
                std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                trees::SentIds::EraseKey(&bySentId_, nullptr, &key);   // 0043BE60
                FreedId value{};
                value.id = entry->id_;
                value.type = entry->type_;
                FreedIds& batch = freedIds_.End()->prev->data;   // the newest batch
                if (batch.end() != batch.EndOfStorage()) {
                    stlp::ConstructAt(batch.end(), &value);   // 0043D7F0
                    ++batch.Finish();
                } else {
                    batch.InsertOverflow<stlp::PlainElement<FreedId>>(batch.end(), value, 1);   // 0043B6C0
                }
                trees::Entries::EraseAt(tree, nullptr, found);
            }
        }
    }
}
EI_REWRITTEN(0x00433FA0, Connection::Delivered);

} // namespace net
