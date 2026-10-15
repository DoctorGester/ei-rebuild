// A connection's tick (00433BD0): clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_library.h"

#include <cstring>

namespace net {

namespace {

inline double D(float f) { return f; }

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The history / acknowledged lists (STLport list<HistoryNode>): end, splice of one node before
// `pos`, erase.
__declspec(naked) HistoryNode** __fastcall HistoryEnd(HistoryNode** /*list*/, void*, HistoryNode** /*out*/) EI_ORIGINAL(0x00437F20)
__declspec(naked) void __fastcall HistorySplice(HistoryNode** /*list*/, void*, HistoryNode* /*pos*/, HistoryNode** /*from*/,
                                                HistoryNode* /*node*/) EI_ORIGINAL(0x004380C0)
// The freed-id queue (list of vector<FreedId>): erase, insert a default element.
__declspec(naked) FreedBatch** __fastcall FreedErase(FreedBatch** /*list*/, void*, FreedBatch** /*out*/, FreedBatch* /*pos*/) EI_ORIGINAL(0x004390C0)
// vector<uint16_t> pieces of the id pools: construct in place, insert when full; vector copy of
// 4-byte elements (returns the end of the copy).
__declspec(naked) void __fastcall IdConstruct(std::uint16_t* /*at*/, const void* /*value*/) EI_ORIGINAL(0x0043D7E0)
__declspec(naked) void __fastcall IdInsertOverflow(IdPool* /*pool*/, void*, std::uint16_t* /*pos*/, const void* /*value*/,
                                                   std::uint32_t /*n*/) EI_ORIGINAL(0x0043B530)
__declspec(naked) FreedId* __fastcall CopyFreed(FreedId* /*first*/, FreedId* /*last*/, FreedId* /*dest*/) EI_ORIGINAL(0x0043DC80)

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

// Float bit select as the original writes min / max: `pick` when the flag holds, else `other`.
float Select(bool flag, float pick, float other) { return flag ? pick : other; }

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
    float loss = static_cast<float>(x87::fsqrt(spread) * D(GameFloat(0x0073BAA4)) + D(roundTrip_));
    float tenth;
    const std::uint32_t tenthBits = 0x3DCCCCCD;   // 0.1f
    std::memcpy(&tenth, &tenthBits, 4);
    loss = Select(!(loss >= tenth), tenth, loss);
    float longest = static_cast<float>(static_cast<double>(CurrentSession()->maxRoundTrip_));
    loss = Select(!(longest >= loss), longest, loss);
    float kept = static_cast<float>(static_cast<double>(CurrentSession()->maxRoundTrip_));

    for (HistoryNode* node = history_->next; node != history_;) {
        double age = Get(&clock_) - Get(&node->data.time);
        if (!(D(loss) < age)) break;
        RollBack(&node->data);
        HistoryNode* next = node->next;
        HistoryNode* end;
        HistoryEnd(&acknowledged_, nullptr, &end);
        HistorySplice(&acknowledged_, nullptr, end, &history_, node);
        node = next;
        if (node == history_) break;
    }
    for (HistoryNode* node = acknowledged_->next; node != acknowledged_;) {
        double age = Get(&clock_) - Get(&node->data.time);
        if (!(D(kept) < age)) {
            node = node->next;
        } else {
            HistoryNode* after;
            HistoryErase(&acknowledged_, nullptr, &after, node);
            node = after;
        }
    }

    double timer = D(sendTimer_) + dt;
    sendTimer_ = static_cast<float>(timer);
    if (timer > D(sendInterval_)) {
        do {
            sendTimer_ = static_cast<float>(D(sendTimer_) - D(sendInterval_));
            if (SendUpdate(static_cast<int>(updateBudget_)) && headers_->next == headers_) {
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
        FreedBatch* oldest = freedIds_->next;
        for (FreedId* e = oldest->begin; e != oldest->end; ++e) {
            std::uint32_t value = e->id;
            IdPool& pool = idPools_[e->type];
            if (pool.end != pool.eos) {
                IdConstruct(pool.end, &value);
                ++pool.end;
            } else {
                IdInsertOverflow(&pool, nullptr, pool.end, &value, 1);
            }
        }
        oldest->end = CopyFreed(oldest->end, oldest->end, oldest->begin);
        FreedBatch* out;
        FreedErase(&freedIds_, nullptr, &out, freedIds_->next);
        FreedInsert(&freedIds_, nullptr, &out, freedIds_);
    }

    bool alive = false;
    if (static_cast<double>(CurrentSession()->timeout_) > D(idleTime_)) alive = ReliableSize(&reliableOut_) < 32000;
    CurrentConnection() = nullptr;
    return alive;
}
EI_REPLACE_METHOD(0x00433BD0, Connection::Tick);

} // namespace net

namespace net {

namespace {
__declspec(naked) Connection::EntryMap::Node** __fastcall PriorityFind(Connection::EntryMap* /*tree*/, void*,
                                                                       Connection::EntryMap::Node** /*out*/,
                                                                       ReplicaEntry* const* /*key*/) EI_ORIGINAL(0x0043C9A0)
__declspec(naked) void __fastcall PriorityEraseAt(Connection::EntryMap* /*tree*/, void*,
                                                  Connection::EntryMap::Node* /*position*/) EI_ORIGINAL(0x00437EF0)
__declspec(naked) std::uint32_t __fastcall BySentIdErase(stlp::map<std::uint32_t, RefPtr>* /*map*/, void*,
                                                         const std::uint32_t* /*key*/) EI_ORIGINAL(0x0043BE60)
// vector<FreedId> of a freed-id batch: construct in place (4 bytes), insert when full.
__declspec(naked) void __fastcall FreedConstruct(FreedId* /*at*/, const FreedId* /*value*/) EI_ORIGINAL(0x0043D7F0)
__declspec(naked) void __fastcall FreedInsertOverflow(FreedId** /*vector*/, void*, FreedId* /*pos*/, const FreedId* /*value*/,
                                                      std::uint32_t /*n*/) EI_ORIGINAL(0x0043B6C0)
float RttKeep() { return GameFloat(0x0073BF30); }   // the share of the old average kept per sample
float One() { return GameFloat(0x0073B84C); }       // 1.0f
} // namespace

void Connection::Delivered(SentPacket* packet) {
    double sample = Get(&clock_) - Get(&packet->time);
    double kept = D(RttKeep()) * D(roundTrip_);
    roundTrip_ = static_cast<float>(kept);
    roundTrip_ = static_cast<float>((D(One()) - D(RttKeep())) * sample + kept);
    double keptSquare = D(RttKeep()) * D(roundTripSquare_);
    roundTripSquare_ = static_cast<float>(keptSquare);
    roundTripSquare_ = static_cast<float>((D(One()) - D(RttKeep())) * sample * sample + keptSquare);
    for (SentItem* item = packet->items->next; item != packet->items; item = item->next) {
        auto action = static_cast<std::int32_t>(item->action);
        if (action == SentItem::kChangedReliable) {
            std::uint32_t version = item->version;
            if (item->entry->version_ > version) version = item->entry->version_;
            item->entry->version_ = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            EntryMap* tree = &priorities_[entry->flags_ >> 1];
            EntryMap::Node* found;
            PriorityFind(tree, nullptr, &found, &entry);
            if (found != tree->end()) {
                entry = item->entry;
                std::uint32_t key = (static_cast<std::uint32_t>(entry->type_) << 16) + entry->id_;
                BySentIdErase(&bySentId_, nullptr, &key);
                FreedId value{};
                value.id = entry->id_;
                value.type = entry->type_;
                FreedBatch* batch = freedIds_->prev;   // the newest batch
                if (batch->end != batch->eos) {
                    FreedConstruct(batch->end, &value);
                    ++batch->end;
                } else {
                    FreedInsertOverflow(&batch->begin, nullptr, batch->end, &value, 1);
                }
                PriorityEraseAt(tree, nullptr, found);
            }
        }
    }
}
EI_REPLACE_METHOD(0x00433FA0, Connection::Delivered);

} // namespace net
