// Connections of the network sessions: object replication. See net_connection.h.
#include "net_connection.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>
#include "originals_core.h"
#include "originals_library.h"

namespace net {

namespace {

using ObjectMap = stlp::map<CRefObject*, RefPtr>;
using SentIdMap = stlp::map<std::uint32_t, RefPtr>;
using AttachedMap = stlp::map<CRefObject*, AttachedId>;
using EntryMap = Connection::EntryMap;
using RecordMap = Connection::RecordMap;

// --- STLport container instantiations (library) -----------------------------------------------
// Lists: insert a default element before `pos`, erase, begin, splice. Iterators are node pointers
// passed by value.
__declspec(naked) HistoryNode** __fastcall HistoryInsert(HistoryNode** /*list*/, void*, HistoryNode** /*out*/, HistoryNode* /*pos*/) EI_ORIGINAL(0x004397E0)
__declspec(naked) SentItem** __fastcall ItemsInsert(SentItem** /*list*/, void*, SentItem** /*out*/, SentItem* /*pos*/) EI_ORIGINAL(0x00438F60)
__declspec(naked) HeaderNode** __fastcall HeadersInsert(HeaderNode** /*list*/, void*, HeaderNode** /*out*/, HeaderNode* /*pos*/) EI_ORIGINAL(0x00439C80)
__declspec(naked) HeaderNode** __fastcall HeadersInsertCopy(HeaderNode** /*list*/, void*, HeaderNode** /*out*/, HeaderNode* /*pos*/,
                                                            const ReliableChunk* /*value*/) EI_ORIGINAL(0x00439C50)
__declspec(naked) HeaderNode** __fastcall HeadersErase(HeaderNode** /*list*/, void*, HeaderNode** /*out*/, HeaderNode* /*pos*/) EI_ORIGINAL(0x00438660)
__declspec(naked) HeaderNode** __fastcall HeadersBegin(HeaderNode** /*list*/, void*, HeaderNode** /*out*/) EI_ORIGINAL(0x00438640)
__declspec(naked) HeaderNode** __fastcall HeadersBeginForInsert(HeaderNode** /*list*/, void*, HeaderNode** /*out*/) EI_ORIGINAL(0x00438650)
__declspec(naked) void __fastcall HeadersSplice(HeaderNode** /*list*/, void*, HeaderNode* /*pos*/, HeaderNode** /*from*/,
                                                HeaderNode* /*first*/, HeaderNode* /*last*/) EI_ORIGINAL(0x00438770)
__declspec(naked) HeaderNode** __fastcall HeaderIteratorCopy(HeaderNode** /*it*/, void*, HeaderNode* const* /*from*/) EI_ORIGINAL(0x004387C0)
__declspec(naked) HistoryNode** __fastcall ListIteratorDecrement(HistoryNode** /*it*/) EI_ORIGINAL(0x00439A60)
// Red-black trees: iterator increment, find / lower_bound, insert with a hint, erase by key,
// operator[].
void __fastcall TreeIncrement(void** a0) { TreeIncrementOriginal((void*)a0); }
__declspec(naked) RecordMap::Node** __fastcall RecordsFind(RecordMap* /*tree*/, void*, RecordMap::Node** /*out*/,
                                                           const std::int32_t* /*key*/) EI_ORIGINAL(0x0043CE70)
__declspec(naked) RecordMap::Node** __fastcall RecordsLowerBound(RecordMap* /*tree*/, void*, RecordMap::Node** /*out*/,
                                                                 const std::int32_t* /*key*/) EI_ORIGINAL(0x00439AA0)
__declspec(naked) RecordMap::Node** __fastcall RecordsIteratorCopy(RecordMap::Node** /*it*/, void*, RecordMap::Node* const* /*from*/) EI_ORIGINAL(0x00439B70)
__declspec(naked) RecordMap::Node** __fastcall RecordsInsertHint(RecordMap* /*tree*/, void*, RecordMap::Node** /*out*/,
                                                                 RecordMap::Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439A70)
__declspec(naked) AttachedMap::Node** __fastcall AttachedLowerBound(AttachedMap* /*tree*/, void*, AttachedMap::Node** /*out*/,
                                                                    CRefObject* const* /*key*/) EI_ORIGINAL(0x0043D3E0)
__declspec(naked) AttachedMap::Node** __fastcall AttachedIteratorCopy(AttachedMap::Node** /*it*/, void*, AttachedMap::Node* const* /*from*/) EI_ORIGINAL(0x00439C40)
__declspec(naked) AttachedMap::Node** __fastcall AttachedInsertHint(AttachedMap* /*tree*/, void*, AttachedMap::Node** /*out*/,
                                                                    AttachedMap::Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439B80)
__declspec(naked) std::uint32_t __fastcall AttachedErase(AttachedMap* /*tree*/, void*, CRefObject* const* /*key*/) EI_ORIGINAL(0x0043D2E0)
__declspec(naked) std::uint32_t __fastcall ByObjectErase(ObjectMap* /*tree*/, void*, CRefObject* const* /*key*/) EI_ORIGINAL(0x0043C3B0)
__declspec(naked) RefPtr* __fastcall BySentIdAt(SentIdMap* /*map*/, void*, const std::uint32_t* /*key*/) EI_ORIGINAL(0x00437B90)
// The object references' lookups: find in byObject_, attached_ and bySentId_; operator[] of
// records_ (returns the mapped Record) and of attached_ (returns the mapped AttachedId); operator[]
// of the type registry's factory table.
__declspec(naked) ObjectMap::Node** __fastcall ByObjectFind(ObjectMap* /*tree*/, void*, ObjectMap::Node** /*out*/,
                                                            CRefObject* const* /*key*/) EI_ORIGINAL(0x0043C450)
__declspec(naked) AttachedMap::Node** __fastcall AttachedFind(AttachedMap* /*tree*/, void*, AttachedMap::Node** /*out*/,
                                                              CRefObject* const* /*key*/) EI_ORIGINAL(0x0043D380)
__declspec(naked) SentIdMap::Node** __fastcall BySentIdFind(SentIdMap* /*tree*/, void*, SentIdMap::Node** /*out*/,
                                                            const std::int32_t* /*key*/) EI_ORIGINAL(0x0043BF00)
__declspec(naked) Record* __fastcall RecordsAt(RecordMap* /*tree*/, void*, const std::int32_t* /*key*/) EI_ORIGINAL(0x004382A0)
__declspec(naked) AttachedId* __fastcall AttachedAt(AttachedMap* /*tree*/, void*, CRefObject* const* /*key*/) EI_ORIGINAL(0x00438530)
__declspec(naked) MessageFactory* __fastcall FactoryAt(TypeRegistry::ByType* /*table*/, void*, const std::uint32_t* /*key*/) EI_ORIGINAL(0x00439EA0)
// vector<int> of received sequences: insert when full.
__declspec(naked) void __fastcall ReceivedInsertOverflow(std::int32_t** /*vector*/, void*, std::int32_t* /*pos*/, const void* /*value*/,
                                                         std::uint32_t /*n*/) EI_ORIGINAL(0x0043B850)
// pair<const int, Record> from a key and a Record (the counted pointer shared).
__declspec(naked) void* __fastcall MakeRecordPair(void* /*pair*/, void*, const std::int32_t* /*key*/, const Record* /*record*/) EI_ORIGINAL(0x0043A090)
// RefPtr<T>: default constructor, assignment of a raw pointer (00438110: the old object is released
// as RefPtr::Destroy does, the new one shared as RefPtr::Share does; returns the holder).
__declspec(naked) RefPtr* __fastcall RefPtrDefault(RefPtr* /*p*/) EI_ORIGINAL(0x00438A90)
__declspec(naked) RefPtr* __fastcall RefPtrReset(RefPtr* /*p*/, void*, CRefObject* /*object*/) EI_ORIGINAL(0x00438110)

__declspec(naked) TypeRegistry::ByVtable::Node** __fastcall TypeFind(TypeRegistry::ByVtable* /*table*/, void*,
                                                                    TypeRegistry::ByVtable::Node** /*out*/,
                                                                    const std::uint32_t* /*key*/) EI_ORIGINAL(0x0042A810)
__declspec(naked) ObjectMap::Node** __fastcall ByObjectLowerBound(ObjectMap* /*tree*/, void*, ObjectMap::Node** /*out*/,
                                                                  Message* const* /*key*/) EI_ORIGINAL(0x0043C4B0)
__declspec(naked) ObjectMap::Node** __fastcall ByObjectIteratorCopy(ObjectMap::Node** /*it*/, void*, ObjectMap::Node* const* /*from*/) EI_ORIGINAL(0x00437D60)
__declspec(naked) ObjectMap::Node** __fastcall ByObjectInsertHint(ObjectMap* /*tree*/, void*, ObjectMap::Node** /*out*/,
                                                                  ObjectMap::Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439540)
__declspec(naked) void __fastcall ByObjectEraseAt(ObjectMap* /*tree*/, void*, ObjectMap::Node* /*position*/) EI_ORIGINAL(0x00437D40)
__declspec(naked) void* __fastcall ByObjectMakePair(void* /*pair*/, void*, Message* const* /*key*/, const RefPtr* /*value*/) EI_ORIGINAL(0x0043A050)
__declspec(naked) EntryMap::Node** __fastcall PriorityLowerBound(EntryMap* /*tree*/, void*, EntryMap::Node** /*out*/,
                                                                 ReplicaEntry* const* /*key*/) EI_ORIGINAL(0x0043CA00)
__declspec(naked) EntryMap::Node** __fastcall PriorityIteratorCopy(EntryMap::Node** /*it*/, void*, EntryMap::Node* const* /*from*/) EI_ORIGINAL(0x00437F10)
__declspec(naked) EntryMap::Node** __fastcall PriorityInsertHint(EntryMap* /*tree*/, void*, EntryMap::Node** /*out*/,
                                                                 EntryMap::Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439730)
__declspec(naked) void* __fastcall PriorityMakePair(void* /*pair*/, void*, ReplicaEntry* const* /*key*/, const RefPtr* /*value*/) EI_ORIGINAL(0x0043A070)
__declspec(naked) std::uint32_t __fastcall PriorityErase(EntryMap* /*tree*/, void*, ReplicaEntry* const* /*key*/) EI_ORIGINAL(0x0043C900)
__declspec(naked) RefPtr* __fastcall RefPtrNull(RefPtr* /*p*/) EI_ORIGINAL(0x00438F50)
// 0041DD50: a RefPtr from a raw pointer, adding a reference.
// The static vector of 004346D0: constructor, erase of a range, introsort loop and final insertion
// sort by object, atexit.
__declspec(naked) void* __fastcall ReplicatedConstruct(ReplicatedVector* /*v*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439E10)
__declspec(naked) void* __fastcall ReplicatedErase(ReplicatedVector* /*v*/, void*, Replicated* /*first*/, Replicated* /*last*/) EI_ORIGINAL(0x00439DD0)
__declspec(naked) void __fastcall ReplicatedSortLoop(Replicated* /*first*/, Replicated* /*last*/, void* /*tag*/, int /*depth*/,
                                                     void* /*compare*/) EI_ORIGINAL(0x0043F870)
__declspec(naked) void __fastcall ReplicatedSortFinish(Replicated* /*first*/, Replicated* /*last*/, void* /*compare*/) EI_ORIGINAL(0x0043F9B0)
constexpr std::uintptr_t kReplicatedDestroyAtExit = 0x00434980;
// 0079B8D0 / 0079B8DC: CollectChanges's static vector of {object, priority} and its guard.
ReplicatedVector& StaticReplicated() { return *reinterpret_cast<ReplicatedVector*>(0x0079B8D0); }
std::uint8_t& StaticReplicatedGuard() { return *reinterpret_cast<std::uint8_t*>(0x0079B8DC); }

// --- Not rewritten yet ---------------------------------------------------------------------------
// 004DA5C0: the growing buffer makes room for `size` bytes.
inline void __fastcall GameBufferGrow(GameBuffer* a0, void*, std::uint32_t a2) { GameBufferGrowOriginal((void*)a0, nullptr, a2); }

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// "text\n" and a flush to the network log, when there is one.
void LogLine(std::uintptr_t text) {
    if (LogStream* log = NetLog()) log->Put(Text(text))->PutChar('\n')->Flush();
}
constexpr std::uintptr_t kSkipUpdate = 0x00783EB0;   // "SKIP update, newer version of object"
constexpr std::uintptr_t kSkipRemove = 0x00783E78;   // "SKIP remove, newer version of object"

// 00419CC0 (inlined helper of the buffers): `high` follows `cur` upwards.
void UpdateHigh(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
}

// Bytes in a buffer (written so far).
std::int32_t BufferedSize(GameBuffer* b) {
    UpdateHigh(b);
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

// A tree iterator moved on (the library's ++).
template <typename Node>
void Increment(Node*& it) {
    TreeIncrement(reinterpret_cast<void**>(&it));
}

// The handlers registered for message `type` on `channel` (__fastcall: session, message). The end
// is read again after each call, as in the original.
using Handler = void(__fastcall*)(SessionBase*, CRefObject*);
void CallHandlers(SessionBase* session, int channel, std::uint32_t type, CRefObject* message) {
    HandlerVector& v = session->handlers_[channel][type & 0xFF];
    for (void** h = v.begin(); h != v.end(); ++h) reinterpret_cast<Handler>(*h)(session, message);
}

// A message's virtual Write (slot 4) and Read (slot 5) over a byte cursor.
void WriteObject(CRefObject* object, BufferCursor* cursor) { static_cast<Message*>(object)->Write(cursor); }
void ReadObject(CRefObject* object, BufferCursor* cursor) { static_cast<Message*>(object)->Read(cursor); }

// The pair the record inserts build.
struct RecordPair {
    std::int32_t key;
    Record record;
};

// records_.insert(hint, pair(key, Record())) as ReadRecord inlines it; returns the new node.
RecordMap::Node* InsertRecord(RecordMap& records, RecordMap::Node* hint, std::int32_t key) {
    Record empty;
    RefPtrDefault(&empty.object);
    empty.version = 0;   // left uninitialised by the original; overwritten by every caller
    RecordPair pair;
    MakeRecordPair(&pair, nullptr, &key, &empty);
    RecordMap::Node* copy;
    RecordsIteratorCopy(&copy, nullptr, &hint);
    RecordMap::Node* inserted;
    RecordsInsertHint(&records, nullptr, &inserted, copy, &pair);
    pair.record.object.Destroy();
    empty.object.Destroy();
    return inserted;
}

// The node for `key` in `records`, inserted when missing (the lower_bound + hinted insert idiom).
RecordMap::Node* RecordAt(RecordMap& records, std::int32_t key) {
    RecordMap::Node* lb;
    RecordsLowerBound(&records, nullptr, &lb, &key);
    if (lb == records.end() || key < lb->first) lb = InsertRecord(records, lb, key);
    return lb;
}

} // namespace

std::uint16_t IdPool::Allocate() {
    if (begin == end) return ++next;
    --end;
    return *end;
}
EI_REPLACE_METHOD(0x00435F10, IdPool::Allocate);

// 00433B10 (no Ghidra function; the client base's 00442A20 jumps to it with its first
// connection). Plain float maths (rewrite-guide rule 7): a silence within rounding of the limit
// may be judged one call apart from the original.
int Connection::Silent() const {
    float factor;
    std::memcpy(&factor, reinterpret_cast<const void*>(0x0073BAA4), sizeof(factor));   // 3.0
    float limit = roundTrip_ * factor;
    return !(limit >= idleTime_) ? 1 : 0;
}
EI_REPLACE_METHOD(0x00433B10, Connection::Silent);

void Connection::WriteAcks(std::uint8_t** p) {
    std::uint16_t base = static_cast<std::uint16_t>(lastReceived_);
    std::uint32_t mask = 0;
    std::int32_t* end = receivedEnd_;
    std::int32_t target = static_cast<std::int32_t>(lastReceived_);
    for (int k = 1; k <= 32; ++k) {
        --target;
        std::int32_t* it = received_;
        while (it != end && *it != target) ++it;
        if (it != end) mask |= 1u << (k - 1);
    }
    std::memcpy(*p, &base, 2);
    *p += 2;
    std::memcpy(*p, &mask, 4);
    *p += 4;
    // Forget sequences more than 32 behind (from the back, shifting the rest down).
    for (std::int32_t i = static_cast<std::int32_t>(receivedEnd_ - received_) - 1; i >= 0; --i) {
        std::int32_t* at = received_ + i;
        if (static_cast<std::uint32_t>(*at) + 0x20 < lastReceived_) {
            for (std::int32_t* q = at; q + 1 < receivedEnd_; ++q) q[0] = q[1];
            --receivedEnd_;
        }
    }
}
EI_REPLACE_METHOD(0x00434C20, Connection::WriteAcks);

void Connection::QueueHeader(int budget) {
    if (headers_->next == headers_) {
        int size = budget < 0xFF ? budget : 0xFF;
        HeaderNode* out;
        HeadersInsert(&headers_, nullptr, &out, headers_);
        std::uint8_t* chunk = headers_->prev->data.bytes;
        int available = BufferedSize(&reliableOut_);
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence_, 2);
        std::memcpy(chunk + 3, reliableOut_.begin, static_cast<std::size_t>(size));
        int remaining = BufferedSize(&reliableOut_) - size;
        std::memmove(reliableOut_.begin, reliableOut_.begin + size, static_cast<std::size_t>(remaining));
        reliableOut_.cur = reliableOut_.begin;
        reliableOut_.high = reliableOut_.begin + remaining;
        if (reliableOut_.high > reliableOut_.end) GameBufferGrow(&reliableOut_, nullptr, static_cast<std::uint32_t>(remaining));
        headerSequence_ = static_cast<std::uint16_t>(headerSequence_ + size);
        return;
    }
    // The oldest waiting chunk (sequence order with wrap-around) moves to the front.
    HeaderNode* head = headers_;
    HeaderNode* best = head->next;
    std::uint16_t bestSeq = best->data.Sequence();
    for (HeaderNode* n = best; n != head; n = n->next) {
        std::uint16_t seq = n->data.Sequence();
        if (static_cast<std::uint16_t>(seq - bestSeq) > 0x8000) {
            best = n;
            bestSeq = seq;
        }
    }
    if (head->next == best) return;
    HeaderNode* last = best->next;
    HeaderNode* first;
    HeaderIteratorCopy(&first, nullptr, &best);
    HeaderNode* position;
    HeadersBegin(&headers_, nullptr, &position);
    HeadersSplice(&headers_, nullptr, position, &headers_, first, last);
}
EI_REPLACE_METHOD(0x00434990, Connection::QueueHeader);

void Connection::RollBack(SentPacket* packet) {
    if (packet->rolledBack) return;
    for (SentItem* item = packet->items->next; item != packet->items; item = item->next) {
        // Unreliable sends are forgotten (the entry is sent again); a reliable change only when
        // the entry still carries the version this packet sent.
        if (item->action == SentItem::kChanged) {
            if (item->entry->version_ == item->version) item->entry->version_ = 0;
        } else if (item->action == SentItem::kRemoved) {
            item->entry->version_ = 0;
        }
    }
    if (packet->header.Size() != 0) {
        HeaderNode* position;
        HeadersBeginForInsert(&headers_, nullptr, &position);
        HeaderNode* out;
        HeadersInsertCopy(&headers_, nullptr, &out, position, &packet->header);
    }
    packet->rolledBack = 1;
}
EI_REPLACE_METHOD(0x00433F20, Connection::RollBack);

void Connection::AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key) {
    AttachedMap::Node* lb;
    AttachedLowerBound(&attached_, nullptr, &lb, &object);
    if (lb == attached_.end() || reinterpret_cast<std::uintptr_t>(object) < reinterpret_cast<std::uintptr_t>(lb->first)) {
        struct {
            CRefObject* key;
            std::uint32_t value;
        } pair{object, type};
        AttachedMap::Node* hint;
        AttachedIteratorCopy(&hint, nullptr, &lb);
        AttachedMap::Node* inserted;
        AttachedInsertHint(&attached_, nullptr, &inserted, hint, &pair);
        lb = inserted;
    }
    lb->second.id = static_cast<std::uint16_t>(key);
    lb->second.type = static_cast<std::uint8_t>(type);
}
EI_REPLACE_METHOD(0x004352F0, Connection::AttachRecord);

bool Connection::SendUpdate(int budget) {
    bool complete = true;
    CollectChanges();
    BufferCursor c;
    c.Reset(nullptr, 0, 0);
    c.sink = nullptr;
    GameBuffer* out = SendBuffer();
    out->Seek(0);
    c.BeginWrite(out, budget + 0x400);

    *c.p = 0;   // packet kind: update
    std::uint8_t* payload = c.p + 1;
    std::uint16_t sequence = sendSequence_++;
    c.p = payload;
    std::memcpy(c.p, &sequence, 2);
    c.p += 2;
    WriteAcks(&c.p);
    QueueHeader(budget - 0x10);

    // The reliable chunk at the front of the queue goes into the packet and into its history entry.
    const ReliableChunk& chunk = headers_->next->data;
    std::size_t chunkSize = static_cast<std::size_t>(chunk.Size()) + 3;
    std::memcpy(c.p, chunk.bytes, chunkSize);
    c.p += chunkSize;
    HistoryNode* inserted;
    HistoryInsert(&history_, nullptr, &inserted, history_);
    HistoryNode* sent = history_->prev;
    std::memcpy(&sent->data.time, &clock_, sizeof(sent->data.time));
    sent->data.sequence = sequence;
    sent->data.header = chunk;
    HeaderNode* afterErase;
    HeadersErase(&headers_, nullptr, &afterErase, headers_->next);

    std::uint8_t* count = c.p++;
    *count = 0;
    for (int priority = 10; priority >= 0; --priority) {
        EntryMap& map = priorities_[priority];
        EntryMap::Node* it = map.begin();
        while (it != map.end()) {
            ReplicaEntry* entry = static_cast<ReplicaEntry*>(it->second.ptr);
            Message* object = entry->Object();
            bool alive;
            if (object && !(object->flags7_ & CRefObject::kDetached)) {
                alive = true;
                if (entry->version_ == object->version_) {
                    Increment(it);
                    continue;
                }
                if (entry->id_ == 0) {
                    entry->id_ = idPools_[entry->type_].Allocate();
                    std::uint32_t key = (static_cast<std::uint32_t>(entry->type_) << 16) + entry->id_;
                    BySentIdAt(&bySentId_, nullptr, &key)->Assign(entry);
                }
                c.PutBits(1, 1);
                std::uint32_t id = entry->id_;
                *c.p = entry->type_;
                ++c.p;
                c.WriteBytes(&id, 2);
                WriteObject(entry->object_.ptr, &c);
            } else {
                alive = false;
                if (entry->id_ == 0) {
                    Increment(it);
                    continue;
                }
                if (entry->object_.ptr) {
                    CRefObject* key = entry->object_.ptr;
                    ByObjectErase(&byObject_, nullptr, &key);
                    entry->object_.Assign(nullptr);
                    entry->version_ = 0;
                }
                if (entry->version_ != 0) {
                    Increment(it);
                    continue;
                }
                c.PutBits(0, 1);
                std::uint32_t id = entry->id_;
                *c.p = entry->type_;
                ++c.p;
                c.WriteBytes(&id, 2);
            }
            if (budget < static_cast<int>(c.p - payload)) {
                // Over the budget: this priority's other entries wait (the entry just written
                // stays in the packet, uncounted).
                complete = false;
                break;
            }
            ++*count;
            SentItem* added;
            ItemsInsert(&sent->data.items, nullptr, &added, sent->data.items);
            SentItem* item = sent->data.items->prev;
            ReplicaEntry* old = item->entry;
            item->entry = entry;
            ++entry->refs_;
            if (old) old->Release();
            if (entry->flags_ & 1) {
                if (alive) {
                    item->action = SentItem::kChangedReliable;
                    item->version = entry->Object()->version_;
                } else {
                    item->action = SentItem::kRemovedReliable;
                }
            } else if (alive) {
                entry->version_ = entry->Object()->version_;
                item->action = SentItem::kChanged;
                item->version = entry->Object()->version_;
            } else {
                entry->version_ = 1;
                item->action = SentItem::kRemoved;
            }
            Increment(it);
        }
    }
    c.End();

    if (CurrentSession()->socket_.SendTo(&address_, out)) return complete;
    // Not sent: undo what the history entry recorded and drop it.
    RollBack(&history_->prev->data);
    HistoryNode* last = history_;
    ListIteratorDecrement(&last);
    HistoryNode* afterHistoryErase;
    HistoryErase(&history_, nullptr, &afterHistoryErase, last);
    return false;
}
EI_REPLACE_METHOD(0x00435A00, Connection::SendUpdate);

bool Connection::ReadRecord(BufferCursor* cursor, std::uint32_t version) {
    std::uint32_t created = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t id = 0;
    cursor->ReadBytes(&id, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (id & 0xFFFF));
    RecordMap::Node* found;
    RecordsFind(&records_, nullptr, &found, &key);
    SessionBase* session = CurrentSession();

    if (!created) {
        // Removal.
        if (found == records_.end()) {
            RecordAt(records_, key)->second.version = version;
            return true;
        }
        if (found->second.version >= version) {
            LogLine(kSkipRemove);
            return true;
        }
        RefPtr* slot = &found->second.object;
        if (slot->ptr) {
            CRefObject* object = slot->ptr;
            AttachedErase(&attached_, nullptr, &object);
            object = slot->ptr;
            session = CurrentSession();
            CallHandlers(session, 2, type, object);
        }
        if (slot->ptr) {
            slot->Destroy();
            slot->Share(nullptr);
        }
        found->second.version = version;
        return true;
    }

    if (found == records_.end()) {
        // A new object.
        RecordMap::Node* node = RecordAt(records_, key);
        CRefObject* object = session->types_.Create(type & 0xFF);
        if (!object) return false;
        RefPtrReset(&node->second.object, nullptr, object);
        node->second.version = 0;
        AttachRecord(object, type, key);
        ReadObject(object, cursor);
        node->second.version = version;
        CallHandlers(CurrentSession(), 0, type, object);
        return true;
    }

    if (found->second.version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine(kSkipUpdate);
        RefPtr scratch;
        scratch.Share(CurrentSession()->types_.Create(type & 0xFF));
        ReadObject(scratch.ptr, cursor);
        scratch.Destroy();
        return true;
    }

    RefPtr* slot = &found->second.object;
    if (!slot->ptr) {
        CRefObject* object = CurrentSession()->types_.Create(type & 0xFF);
        if (object) {
            RefPtrReset(slot, nullptr, object);
            found->second.version = 0;
            AttachRecord(object, type, key);
        }
        if (object != slot->ptr) {
            slot->Destroy();
            slot->Share(object);
        }
        if (!slot->ptr) return false;
    }
    ReadObject(slot->ptr, cursor);
    CRefObject* object = slot->ptr;
    CallHandlers(CurrentSession(), found->second.version == 0 ? 0 : 1, type, object);
    found->second.version = version;
    return true;
}
EI_REPLACE_METHOD(0x00435F30, Connection::ReadRecord);

void Connection::Track(Replicated* item) {
    Message* object = item->object;
    if (!object || (object->flags7_ & CRefObject::kDetached)) return;
    std::uint32_t objectVtable;
    std::memcpy(&objectVtable, object, 4);
    TypeRegistry::ByVtable::Node* found[2];
    TypeFind(&CurrentSession()->types_.byVtable, nullptr, found, &objectVtable);
    std::int32_t type = found[0] ? static_cast<std::int32_t>(found[0]->value) : -1;

    // A new entry (vtable 0073BF4C) with no version and no id yet.
    auto* entry = NewMessageMemory<ReplicaEntry>();
    if (entry) {
        ei::SetVtable(entry, kRefValueVtable);
        RefPtrFromRawOriginal(&entry->object_, nullptr, nullptr);
        ei::SetVtable(entry, ReplicaEntry::kVtableAddress);
        entry->version_ = 0;
        entry->id_ = 0;
    }

    ObjectMap::Node* lb;
    ByObjectLowerBound(&byObject_, nullptr, &lb, &item->object);
    if (lb == byObject_.end() || reinterpret_cast<std::uintptr_t>(item->object) < reinterpret_cast<std::uintptr_t>(lb->first)) {
        RefPtr none;
        RefPtrNull(&none);
        struct {
            void* key;
            RefPtr value;
        } pair;
        ByObjectMakePair(&pair, nullptr, &item->object, &none);
        ObjectMap::Node* hint;
        ByObjectIteratorCopy(&hint, nullptr, &lb);
        ObjectMap::Node* inserted;
        ByObjectInsertHint(&byObject_, nullptr, &inserted, hint, &pair);
        lb = inserted;
        if (pair.value.ptr) pair.value.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    CRefObject*& held = lb->second.ptr;
    CRefObject* old = held;
    held = entry;
    if (entry) ++held->refs_;
    if (old) old->Release();

    EntryMap& map = priorities_[static_cast<std::int8_t>(item->priority)];
    EntryMap::Node* lb2;
    PriorityLowerBound(&map, nullptr, &lb2, &entry);
    if (lb2 == map.end() || reinterpret_cast<std::uintptr_t>(entry) < reinterpret_cast<std::uintptr_t>(lb2->first)) {
        RefPtr none;
        RefPtrNull(&none);
        struct {
            void* key;
            RefPtr value;
        } pair;
        PriorityMakePair(&pair, nullptr, &entry, &none);
        EntryMap::Node* hint;
        PriorityIteratorCopy(&hint, nullptr, &lb2);
        EntryMap::Node* inserted;
        PriorityInsertHint(&map, nullptr, &inserted, hint, &pair);
        lb2 = inserted;
        if (pair.value.ptr) pair.value.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    lb2->second.Assign(entry);
    entry->type_ = static_cast<std::uint8_t>(type);
    CRefObject* previous = entry->object_.ptr;
    entry->object_.ptr = item->object;
    if (entry->object_.ptr) ++entry->object_.ptr->refs_;
    if (previous) previous->Release();
    entry->flags_ = static_cast<std::uint8_t>((entry->flags_ & 1) | (item->priority << 1));
    std::uint8_t reliable = CurrentSession()->reliable_[type];
    entry->flags_ = static_cast<std::uint8_t>(((reliable ^ entry->flags_) & 1) ^ entry->flags_);
}
EI_REPLACE_METHOD(0x00434110, Connection::Track);

void Connection::CollectChanges() {
    ReplicatedVector& items = StaticReplicated();
    if (!(StaticReplicatedGuard() & 1)) {
        StaticReplicatedGuard() |= 1;
        char allocator = 0;
        ReplicatedConstruct(&items, nullptr, &allocator);
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kReplicatedDestroyAtExit));
    }
    ReplicatedErase(&items, nullptr, items.begin(), items.end());
    ReplicatedVector* target = &items;
    CurrentSession()->CollectReplicated(&target);
    if (items.begin() != items.end()) {
        int depth = 0;
        for (std::int32_t n = static_cast<std::int32_t>(items.size()); n != 1; n >>= 1) ++depth;
        ReplicatedSortLoop(items.begin(), items.end(), nullptr, depth * 2, target);
        ReplicatedSortFinish(items.begin(), items.end(), target);
    }

    // Merge the sorted list with byObject_ (both ordered by object pointer).
    Replicated* item = items.begin();
    Replicated* end = items.end();
    ObjectMap::Node* header = byObject_.end();
    ObjectMap::Node* node = byObject_.begin();
    auto entryAt = [](ObjectMap::Node* n) { return static_cast<ReplicaEntry*>(n->second.ptr); };
    if (item != end) {
        do {
            if (node == header) {
                for (; item < end; ++item) Track(item);
                return;
            }
            ReplicaEntry* entry = entryAt(node);
            auto listed = reinterpret_cast<std::uintptr_t>(item->object);
            auto tracked = reinterpret_cast<std::uintptr_t>(entry->object_.ptr);
            if (listed == tracked) {
                Increment(node);
                ++item;
            } else if (listed < tracked) {
                Track(item);
                ++item;
            } else {
                // Tracked but no longer listed: never sent -> forgotten; sent -> marked gone.
                ObjectMap::Node* gone = node;
                Increment(node);
                entry = entryAt(gone);
                if (entry->id_ == 0) {
                    PriorityErase(&priorities_[entry->flags_ >> 1], nullptr, &entry);
                } else {
                    CRefObject* old = entry->object_.ptr;
                    entry->object_.ptr = nullptr;
                    if (old) old->Release();
                    entry->version_ = 0;
                }
                ByObjectEraseAt(&byObject_, nullptr, gone);
            }
        } while (item != end);
    }
    while (node != header) {
        ObjectMap::Node* gone = node;
        Increment(node);
        ReplicaEntry* entry = entryAt(gone);
        if (entry->id_ == 0) {
            PriorityErase(&priorities_[entry->flags_ >> 1], nullptr, &entry);
        } else {
            entry->object_.Assign(nullptr);
            entry->version_ = 0;
        }
        ByObjectEraseAt(&byObject_, nullptr, gone);
    }
}
EI_REPLACE_METHOD(0x004346D0, Connection::CollectChanges);

} // namespace net

void BufferCursor::PutBit(std::uint32_t bit) {
    if (bits == 0) {
        bits = 1;
        bytePtr = p;
        ++p;
        acc = bit;
    } else {
        acc += bit << bits;
        ++bits;
    }
    if (bits > 8) {
        *bytePtr = static_cast<std::uint8_t>(acc);
        bits = static_cast<std::uint8_t>(bits - 8);
        bytePtr = p;
        acc >>= 8;
        ++p;
    }
}
EI_REPLACE_METHOD(0x004357C0, BufferCursor::PutBit);

namespace net {

namespace {
// The entries' 16-bit reference count, as the original inlines RefPtr<ReplicaEntry> assignment:
// the old entry is deleted (slot 0) when its count reaches zero.
void AssignEntry(RefPtr* slot, ReplicaEntry* entry) {
    CRefObject* old = slot->ptr;
    slot->ptr = entry;
    if (entry) ++entry->refs_;
    if (old && --old->refs_ == 0) delete old;
}
bool Usable(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }
} // namespace

void Connection::WriteObjectRef(BufferCursor* cursor, CRefObject* object) {
    if (Usable(object)) {
        ObjectMap::Node* found;
        ByObjectFind(&byObject_, nullptr, &found, &object);
        if (found != byObject_.end()) {
            auto* entry = static_cast<ReplicaEntry*>(found->second.ptr);
            if (entry->id_ == 0) {
                entry->id_ = idPools_[entry->type_].Allocate();
                std::uint32_t key = (static_cast<std::uint32_t>(entry->type_) << 16) + entry->id_;
                AssignEntry(BySentIdAt(&bySentId_, nullptr, &key), entry);
            }
            std::uint16_t id = entry->id_;
            std::uint8_t type = entry->type_;
            cursor->PutBit(0);
            cursor->WriteBytes(&type, 1);
            cursor->WriteBytes(&id, 2);
            return;
        }
        AttachedMap::Node* node;
        AttachedFind(&attached_, nullptr, &node, &object);
        if (node != attached_.end()) {
            std::uint16_t id = node->second.id;
            std::uint8_t type = node->second.type;
            cursor->PutBit(1);
            *cursor->p++ = type;
            std::memcpy(cursor->p, &id, 2);
            cursor->p += 2;
            return;
        }
    }
    std::uint32_t zero = 0;
    cursor->PutBit(0);
    *cursor->p++ = 0;
    cursor->WriteBytes(&zero, 2);
}
EI_REPLACE_METHOD(0x004355B0, Connection::WriteObjectRef);

// The original reads the type and id into dwords whose upper bytes it masks off (and passes the
// type dword on to AttachRecord, which keeps only its low byte in use); they start as zero here.
CRefObject* Connection::ReadObjectRef(BufferCursor* cursor) {
    std::uint32_t sent = cursor->GetBit();
    std::uint32_t type = 0;
    cursor->ReadBytes(&type, 1);
    std::uint32_t id = 0;
    cursor->ReadBytes(&id, 2);
    std::int32_t key = static_cast<std::int32_t>(((type & 0xFF) << 16) + (id & 0xFFFF));
    if ((id & 0xFFFF) == 0) return nullptr;
    if (sent) {
        SentIdMap::Node* node;
        BySentIdFind(&bySentId_, nullptr, &node, &key);
        if (node == bySentId_.end()) return nullptr;
        return static_cast<ReplicaEntry*>(node->second.ptr)->object_.ptr;
    }
    RecordMap::Node* found;
    RecordsFind(&records_, nullptr, &found, &key);
    if (found == records_.end()) {
        Record* record = RecordsAt(&records_, nullptr, &key);
        CRefObject* object = CurrentSession()->types_.Create(type & 0xFF);
        if (!object) return nullptr;
        if (object != record->object.ptr) {
            record->object.Destroy();
            record->object.Share(object);
        }
        record->version = 0;
        AttachRecord(object, type & 0xFF, key);
        return object;
    }
    if (Usable(found->second.object.ptr)) return found->second.object.ptr;
    std::uint32_t typeKey = type & 0xFF;
    MessageFactory factory = *FactoryAt(&CurrentSession()->types_.byType, nullptr, &typeKey);
    CRefObject* object = factory ? factory() : nullptr;
    if (!object) return nullptr;
    RefPtr* slot = &found->second.object;
    if (object != slot->ptr) {
        slot->Destroy();
        slot->Share(object);
    }
    found->second.version = 0;
    AttachedId* attachedId = AttachedAt(&attached_, nullptr, &object);
    attachedId->id = static_cast<std::uint16_t>(key);
    attachedId->type = static_cast<std::uint8_t>(type);
    return object;
}
EI_REPLACE_METHOD(0x00435820, Connection::ReadObjectRef);

} // namespace net

namespace net {

namespace {
constexpr std::uintptr_t kTooOld = 0x00783E5C;      // "SKIP too old update, dif "
constexpr std::uintptr_t kDuplicate = 0x00783E3C;   // "DUPLICATE update received from "
} // namespace

void Connection::Acknowledge(std::uint16_t sequence) {
    for (HistoryNode* node = history_->next; node != history_; node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            HistoryNode* after;
            HistoryErase(&history_, nullptr, &after, node);
            return;
        }
    }
    for (HistoryNode* node = acknowledged_->next; node != acknowledged_; node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            HistoryNode* after;
            HistoryErase(&acknowledged_, nullptr, &after, node);
            return;
        }
    }
}
EI_REPLACE_METHOD(0x00434CE0, Connection::Acknowledge);

// ackMask_ shifts along when the peer's newest acknowledged sequence moves.
void Connection::ReadAcks(BufferCursor* cursor) {
    std::uint16_t sequence;
    std::memcpy(&sequence, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t mask;
    std::memcpy(&mask, cursor->p, 4);
    cursor->p += 4;
    std::int32_t moved = static_cast<std::int32_t>((static_cast<std::uint32_t>(sequence) - ackBase_) & 0xFFFF);
    if (moved > 0x8000) moved -= 0x10000;
    if (moved <= 0) {
        if (moved < -0x20) ackMask_ = 0;
        else ackMask_ >>= (-moved) & 31;   // SHR by CL: a shift of 32 leaves it as it is
    } else {
        std::uint32_t shifted = (ackMask_ << 1) | 1;
        ackMask_ = shifted;
        if (moved - 1 < 0x20) ackMask_ = shifted << ((moved - 1) & 31);
        else ackMask_ = 0;
    }
    std::uint32_t fresh = ~ackMask_ & mask;
    ackBase_ = sequence;
    ackMask_ |= fresh;
    Acknowledge(sequence);
    std::uint16_t older = sequence;
    for (int k = 1; k <= 32; ++k) {
        --older;
        if (fresh & (1u << (k - 1))) Acknowledge(older);
    }
}
EI_REPLACE_METHOD(0x00434D70, Connection::ReadAcks);

bool Connection::Accept(std::uint32_t sequence) {
    std::uint32_t newest = lastReceived_;
    if (sequence + 0x20 < newest) {
        if (LogStream* log = NetLog())
            log->Put(Text(kTooOld))->PutInt(static_cast<int>(newest - sequence))->PutChar('\n')->Flush();
        return false;
    }
    if (!(newest > sequence)) newest = sequence;
    lastReceived_ = newest;
    for (std::int32_t* it = received_; it != receivedEnd_; ++it) {
        if (*it == static_cast<std::int32_t>(sequence)) {
            if (NetLog()) {
                StringRep peer;
                const char* text = address_.ToString(&peer)->start;
                NetLog()->Put(Text(kDuplicate))->Put(text)->PutChar('\n')->Flush();
                if (peer.start) stlp::deallocate(peer.start, static_cast<std::size_t>(peer.eos - peer.start));
            }
            return false;
        }
    }
    if (receivedEnd_ == receivedEos_) {
        ReceivedInsertOverflow(&received_, nullptr, receivedEnd_, &sequence, 1);
    } else {
        if (receivedEnd_) *receivedEnd_ = static_cast<std::int32_t>(sequence);
        ++receivedEnd_;
    }
    return true;
}
EI_REPLACE_METHOD(0x00434E50, Connection::Accept);

void Connection::ProcessHeaders() {
    bool progress;
    do {
        progress = false;
        HeaderNode* node = incoming_->next;
        if (node == incoming_) return;
        do {
            if (node->data.Sequence() == incomingSequence_) {
                GameBuffer* in = &reliableIn_;
                if (in->cur > in->high) in->high = in->cur;
                in->Seek(in->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(in->begin)) +
                         static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(in->high)));
                std::uint32_t size = node->data.Size();
                const std::uint8_t* from = node->data.bytes + 3;
                if (in->cur + size > in->end) {
                    in->WriteMore(from, size);
                } else {
                    in->flags |= 1;
                    std::memcpy(in->cur, from, size);
                    in->cur += size;
                }
                incomingSequence_ = static_cast<std::uint16_t>(incomingSequence_ + node->data.Size());
                HeaderNode* after;
                HeadersErase(&incoming_, nullptr, &after, node);
                node = after;
                progress = true;
            } else {
                node = node->next;
            }
        } while (node != incoming_);
    } while (progress);
}
EI_REPLACE_METHOD(0x00434AE0, Connection::ProcessHeaders);

bool Connection::ReadUpdate(BufferCursor* cursor) {
    CurrentConnection() = this;
    idleTime_ = 0.0f;
    std::uint16_t low;
    std::memcpy(&low, cursor->p, 2);
    cursor->p += 2;
    std::uint32_t sequence = low + sequenceHigh_;
    if (lastReceived_ == 0) lastReceived_ = sequence;
    std::int32_t step = static_cast<std::int32_t>(sequence - lastReceived_);
    if (step > 0x8000) {
        sequence -= 0x10000;
    } else if (step < -0x8000) {
        sequence += 0x10000;
        sequenceHigh_ += 0x10000;
    }
    if (!Accept(sequence)) {
        CurrentConnection() = nullptr;
        return true;
    }
    ReadAcks(cursor);
    ReliableChunk header;
    std::memcpy(header.bytes, cursor->p, 3);
    std::uint8_t* from = cursor->p + 3;
    cursor->p = from;
    std::memcpy(header.bytes + 3, from, header.Size());
    cursor->p = from + header.Size();
    if (static_cast<std::uint16_t>(header.Sequence() - incomingSequence_) <= 0x8000 && header.Size() != 0) {
        HeaderNode* position;
        HeadersBeginForInsert(&incoming_, nullptr, &position);
        HeaderNode* inserted;
        HeadersInsertCopy(&incoming_, nullptr, &inserted, position, &header);
        ProcessHeaders();
    }
    std::uint8_t count = *cursor->p++;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (!ReadRecord(cursor, sequence)) {
            CurrentConnection() = nullptr;
            return false;
        }
    }
    CurrentConnection() = nullptr;
    return true;
}
EI_REPLACE_METHOD(0x00436470, Connection::ReadUpdate);

void Connection::Close() {
    CurrentConnection() = this;
    RecordMap::Node* it = records_.begin();
    while (it != records_.end()) {
        RefPtr* slot = &it->second.object;
        if (CRefObject* object = slot->ptr) {
            std::uint32_t objectVtable = *reinterpret_cast<const std::uint32_t*>(object);
            SessionBase* session = CurrentSession();
            TypeRegistry::ByVtable::Node* found[2];   // the iterator: node, table
            TypeFind(&session->types_.byVtable, nullptr, found, &objectVtable);
            std::uint32_t type = found[0] ? found[0]->value : 0xFFFFFFFFu;
            CallHandlers(session, 2, type, object);
        }
        if (slot->ptr) {
            slot->Destroy();
            slot->ptr = nullptr;
        }
        Increment(it);
    }
    CurrentConnection() = nullptr;
}
EI_REPLACE_METHOD(0x00436620, Connection::Close);

} // namespace net
