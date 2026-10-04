// Connections of the network sessions: object replication. See net_connection.h.
#include "net_connection.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// --- STLport container instantiations (library) -----------------------------------------------
// Lists: insert a default element before `pos`, erase, begin, splice. Iterators are node pointers
// passed by value.
__declspec(naked) void* __fastcall HistoryInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x004397E0)
__declspec(naked) void* __fastcall HistoryErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00437F30)
__declspec(naked) void* __fastcall ItemsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00438F60)
__declspec(naked) void* __fastcall HeadersInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00439C80)
__declspec(naked) void* __fastcall HeadersInsertCopy(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                     const void* /*value*/) EI_ORIGINAL(0x00439C50)
__declspec(naked) void* __fastcall HeadersErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00438660)
__declspec(naked) void** __fastcall HeadersBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00438640)
__declspec(naked) void** __fastcall HeadersBeginForInsert(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00438650)
__declspec(naked) void __fastcall HeadersSplice(void* /*list*/, void*, void* /*pos*/, void* /*from*/, void* /*first*/,
                                                void* /*last*/) EI_ORIGINAL(0x00438770)
__declspec(naked) void** __fastcall HeaderIteratorCopy(void** /*it*/, void*, void* const* /*from*/) EI_ORIGINAL(0x004387C0)
__declspec(naked) void** __fastcall ListIteratorDecrement(void** /*it*/) EI_ORIGINAL(0x00439A60)
// Red-black trees: iterator increment, find / lower_bound, insert with a hint, erase by key,
// operator[].
__declspec(naked) void** __fastcall TreeIncrement(void** /*it*/) EI_ORIGINAL(0x0043DBE0)
__declspec(naked) void* __fastcall RecordsFind(RbTree* /*tree*/, void*, RecordNode** /*out*/, const std::int32_t* /*key*/) EI_ORIGINAL(0x0043CE70)
__declspec(naked) void* __fastcall RecordsLowerBound(RbTree* /*tree*/, void*, RecordNode** /*out*/, const std::int32_t* /*key*/) EI_ORIGINAL(0x00439AA0)
__declspec(naked) RecordNode** __fastcall RecordsIteratorCopy(RecordNode** /*it*/, void*, RecordNode* const* /*from*/) EI_ORIGINAL(0x00439B70)
__declspec(naked) RecordNode** __fastcall RecordsInsertHint(RbTree* /*tree*/, void*, RecordNode** /*out*/, RecordNode* /*hint*/,
                                                           const void* /*pair*/) EI_ORIGINAL(0x00439A70)
__declspec(naked) void* __fastcall AttachedLowerBound(RbTree* /*tree*/, void*, std::uint8_t** /*out*/, CRefObject* const* /*key*/) EI_ORIGINAL(0x0043D3E0)
__declspec(naked) std::uint8_t** __fastcall AttachedIteratorCopy(std::uint8_t** /*it*/, void*, std::uint8_t* const* /*from*/) EI_ORIGINAL(0x00439C40)
__declspec(naked) std::uint8_t** __fastcall AttachedInsertHint(RbTree* /*tree*/, void*, std::uint8_t** /*out*/, std::uint8_t* /*hint*/,
                                                              const void* /*pair*/) EI_ORIGINAL(0x00439B80)
__declspec(naked) std::uint32_t __fastcall AttachedErase(RbTree* /*tree*/, void*, CRefObject* const* /*key*/) EI_ORIGINAL(0x0043D2E0)
__declspec(naked) std::uint32_t __fastcall ByObjectErase(RbTree* /*tree*/, void*, CRefObject* const* /*key*/) EI_ORIGINAL(0x0043C3B0)
__declspec(naked) RefPtr* __fastcall BySentIdAt(void* /*map*/, void*, const std::uint32_t* /*key*/) EI_ORIGINAL(0x00437B90)
// pair<const int, Record> from a key and a Record (the counted pointer shared).
__declspec(naked) void* __fastcall MakeRecordPair(void* /*pair*/, void*, const std::int32_t* /*key*/, const void* /*record*/) EI_ORIGINAL(0x0043A090)
// RefPtr<T>: default constructor, assignment of a raw pointer (00438110: the old object is released
// as RefPtr::Destroy does, the new one shared as RefPtr::Share does; returns the holder).
__declspec(naked) RefPtr* __fastcall RefPtrDefault(RefPtr* /*p*/) EI_ORIGINAL(0x00438A90)
__declspec(naked) RefPtr* __fastcall RefPtrReset(RefPtr* /*p*/, void*, CRefObject* /*object*/) EI_ORIGINAL(0x00438110)

__declspec(naked) void* __fastcall TypeFind(HashTable* /*table*/, void*, void** /*out*/, const std::uint32_t* /*key*/) EI_ORIGINAL(0x0042A810)
__declspec(naked) void* __fastcall ByObjectLowerBound(RbTree* /*tree*/, void*, void** /*out*/, void* const* /*key*/) EI_ORIGINAL(0x0043C4B0)
__declspec(naked) void** __fastcall ByObjectIteratorCopy(void** /*it*/, void*, void* const* /*from*/) EI_ORIGINAL(0x00437D60)
__declspec(naked) void** __fastcall ByObjectInsertHint(RbTree* /*tree*/, void*, void** /*out*/, void* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439540)
__declspec(naked) void __fastcall ByObjectEraseAt(RbTree* /*tree*/, void*, void* /*position*/) EI_ORIGINAL(0x00437D40)
__declspec(naked) void* __fastcall ByObjectMakePair(void* /*pair*/, void*, void* const* /*key*/, const RefPtr* /*value*/) EI_ORIGINAL(0x0043A050)
__declspec(naked) void* __fastcall PriorityLowerBound(RbTree* /*tree*/, void*, void** /*out*/, void* const* /*key*/) EI_ORIGINAL(0x0043CA00)
__declspec(naked) void** __fastcall PriorityIteratorCopy(void** /*it*/, void*, void* const* /*from*/) EI_ORIGINAL(0x00437F10)
__declspec(naked) void** __fastcall PriorityInsertHint(RbTree* /*tree*/, void*, void** /*out*/, void* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x00439730)
__declspec(naked) void* __fastcall PriorityMakePair(void* /*pair*/, void*, void* const* /*key*/, const RefPtr* /*value*/) EI_ORIGINAL(0x0043A070)
__declspec(naked) std::uint32_t __fastcall PriorityErase(RbTree* /*tree*/, void*, void* const* /*key*/) EI_ORIGINAL(0x0043C900)
__declspec(naked) RefPtr* __fastcall RefPtrNull(RefPtr* /*p*/) EI_ORIGINAL(0x00438F50)
// 0041DD50: a RefPtr from a raw pointer, adding a reference.
__declspec(naked) RefPtr* __fastcall RefPtrRaw(RefPtr* /*p*/, void*, CRefObject* /*object*/) EI_ORIGINAL(0x0041DD50)
// The static vector of 004346D0: constructor, erase of a range, introsort loop and final insertion
// sort by object, atexit.
__declspec(naked) void* __fastcall ReplicatedConstruct(ReplicatedVector* /*v*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439E10)
__declspec(naked) void* __fastcall ReplicatedErase(ReplicatedVector* /*v*/, void*, Replicated* /*first*/, Replicated* /*last*/) EI_ORIGINAL(0x00439DD0)
__declspec(naked) void __fastcall ReplicatedSortLoop(Replicated* /*first*/, Replicated* /*last*/, void* /*tag*/, int /*depth*/,
                                                     void* /*compare*/) EI_ORIGINAL(0x0043F870)
__declspec(naked) void __fastcall ReplicatedSortFinish(Replicated* /*first*/, Replicated* /*last*/, void* /*compare*/) EI_ORIGINAL(0x0043F9B0)
__declspec(naked) int __cdecl GameAtExit(void* /*fn*/) EI_ORIGINAL(0x006E727B)
constexpr std::uintptr_t kReplicatedDestroyAtExit = 0x00434980;
ReplicatedVector& StaticReplicated() { return *reinterpret_cast<ReplicatedVector*>(0x0079B8D0); }
std::uint8_t& StaticReplicatedGuard() { return *reinterpret_cast<std::uint8_t*>(0x0079B8DC); }

// --- Not rewritten yet ---------------------------------------------------------------------------
// 00440B90: sendto() of the buffer's contents to `address`; true when all of it went.
__declspec(naked) bool __fastcall SocketSend(SocketDriver* /*driver*/, void*, const void* /*address*/,
                                             GameBuffer* /*buffer*/) EI_ORIGINAL(0x00440B90)
// 004DA5C0: the growing buffer makes room for `size` bytes.
__declspec(naked) void __fastcall GameBufferGrow(GameBuffer* /*buffer*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004DA5C0)

// The old iostream inserters of the network log (library).
struct LogStream {
    LogStream* Put(const char* text);        // 006FC31D
    LogStream* PutChar(char c);              // 006FC0B0
    LogStream* Apply(void* manipulator);     // 00434FE0
};
__declspec(naked) LogStream* LogStream::Put(const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) LogStream* LogStream::PutChar(char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) LogStream* LogStream::Apply(void*) EI_ORIGINAL(0x00434FE0)
constexpr std::uintptr_t kFlush = 0x00435000;   // the manipulator: flushes the stream

// "text\n" and a flush to the network log, when there is one.
void LogLine(std::uintptr_t text) {
    auto* log = static_cast<LogStream*>(NetLog());
    if (!log) return;
    log->Put(reinterpret_cast<const char*>(text))->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
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

void* Sentinel(void* list) { return *static_cast<void**>(list); }
template <typename T> T* NextOf(void* node) { return *static_cast<T**>(node); }
template <typename T> T* LastOf(void* list) { return static_cast<T**>(Sentinel(list))[1]; }

// The handlers registered for message `type` on `channel` (__fastcall: session, message). The end
// is read again after each call, as in the original.
using Handler = void(__fastcall*)(SessionBase*, CRefObject*);
void CallHandlers(SessionBase* session, int channel, std::uint32_t type, CRefObject* message) {
    HandlerVector& v = session->handlers[channel][type & 0xFF];
    for (void** h = v.begin; h != v.end; ++h) reinterpret_cast<Handler>(*h)(session, message);
}

// A message's virtual Write (slot 4) and Read (slot 5) over a byte cursor.
void WriteObject(CRefObject* object, BufferCursor* cursor) { ei::VCall<void>(object, 4, cursor); }
void ReadObject(CRefObject* object, BufferCursor* cursor) { ei::VCall<void>(object, 5, cursor); }

// The red-black tree header's leftmost node.
void* TreeBegin(RbTree& tree) { return static_cast<void**>(tree.header)[2]; }

// A Record as map<int, Record> stores it, and the pair the inserts build.
struct Record {
    RefPtr object;
    std::uint32_t version;
};
struct RecordPair {
    std::int32_t key;
    Record record;
};

// records.insert(hint, pair(key, Record())) as ReadRecord inlines it; returns the new node.
RecordNode* InsertRecord(RbTree& records, RecordNode* hint, std::int32_t key) {
    Record empty;
    RefPtrDefault(&empty.object);
    empty.version = 0;   // left uninitialised by the original; overwritten by every caller
    RecordPair pair;
    MakeRecordPair(&pair, nullptr, &key, &empty);
    RecordNode* copy;
    RecordsIteratorCopy(&copy, nullptr, &hint);
    RecordNode* inserted;
    RecordsInsertHint(&records, nullptr, &inserted, copy, &pair);
    pair.record.object.Destroy();
    empty.object.Destroy();
    return inserted;
}

// The node for `key` in `records`, inserted when missing (the lower_bound + hinted insert idiom).
RecordNode* RecordAt(RbTree& records, std::int32_t key) {
    RecordNode* lb;
    RecordsLowerBound(&records, nullptr, &lb, &key);
    if (lb == records.header || key < lb->key) lb = InsertRecord(records, lb, key);
    return lb;
}

} // namespace

std::uint16_t IdPool::Allocate() {
    if (begin == end) return ++next;
    --end;
    return *end;
}
EI_REPLACE_METHOD(0x00435F10, IdPool::Allocate);

void Connection::WriteAcks(std::uint8_t** p) {
    std::uint16_t base = static_cast<std::uint16_t>(lastReceived);
    std::uint32_t mask = 0;
    std::int32_t* end = receivedEnd;
    std::int32_t target = static_cast<std::int32_t>(lastReceived);
    for (int k = 1; k <= 32; ++k) {
        --target;
        std::int32_t* it = received;
        while (it != end && *it != target) ++it;
        if (it != end) mask |= 1u << (k - 1);
    }
    std::memcpy(*p, &base, 2);
    *p += 2;
    std::memcpy(*p, &mask, 4);
    *p += 4;
    // Forget sequences more than 32 behind (from the back, shifting the rest down).
    for (std::int32_t i = static_cast<std::int32_t>(receivedEnd - received) - 1; i >= 0; --i) {
        std::int32_t* at = received + i;
        if (static_cast<std::uint32_t>(*at) + 0x20 < lastReceived) {
            for (std::int32_t* q = at; q + 1 < receivedEnd; ++q) q[0] = q[1];
            --receivedEnd;
        }
    }
}
EI_REPLACE_METHOD(0x00434C20, Connection::WriteAcks);

void Connection::QueueHeader(int budget) {
    void* sentinel = Sentinel(headers);
    if (NextOf<void>(sentinel) == sentinel) {
        int size = budget < 0xFF ? budget : 0xFF;
        void* out;
        HeadersInsert(&headers, nullptr, &out, Sentinel(&headers));
        std::uint8_t* chunk = LastOf<HeaderNode>(&headers)->data;
        int available = BufferedSize(&reliableOut);
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence, 2);
        std::memcpy(chunk + 3, reliableOut.begin, static_cast<std::size_t>(size));
        int remaining = BufferedSize(&reliableOut) - size;
        std::memmove(reliableOut.begin, reliableOut.begin + size, static_cast<std::size_t>(remaining));
        reliableOut.cur = reliableOut.begin;
        reliableOut.high = reliableOut.begin + remaining;
        if (reliableOut.high > reliableOut.end) GameBufferGrow(&reliableOut, nullptr, static_cast<std::uint32_t>(remaining));
        headerSequence = static_cast<std::uint16_t>(headerSequence + size);
        return;
    }
    // The oldest waiting chunk (sequence order with wrap-around) moves to the front.
    auto* head = static_cast<HeaderNode*>(Sentinel(&headers));
    HeaderNode* best = head->next;
    std::uint16_t bestSeq;
    std::memcpy(&bestSeq, best->data, 2);
    for (HeaderNode* n = best; n != head; n = n->next) {
        std::uint16_t seq;
        std::memcpy(&seq, n->data, 2);
        if (static_cast<std::uint16_t>(seq - bestSeq) > 0x8000) {
            best = n;
            bestSeq = seq;
        }
    }
    if (head->next == best) return;
    void* last = best->next;
    void* first;
    HeaderIteratorCopy(&first, nullptr, reinterpret_cast<void* const*>(&best));
    void* position;
    HeadersBegin(&headers, nullptr, &position);
    HeadersSplice(&headers, nullptr, position, &headers, first, last);
}
EI_REPLACE_METHOD(0x00434990, Connection::QueueHeader);

void Connection::RollBack(std::uint8_t* historyData) {
    auto* node = reinterpret_cast<HistoryNode*>(historyData - 8);
    if (node->rolledBack) return;
    for (SentItem* item = node->items->next; item != node->items; item = item->next) {
        // Unreliable sends are forgotten (the entry is sent again); a reliable change only when
        // the entry still carries the version this packet sent.
        if (item->action == 0) {
            if (item->entry->version == item->version) item->entry->version = 0;
        } else if (item->action == 2) {
            item->entry->version = 0;
        }
    }
    if (node->header[2] != 0) {
        void* position;
        HeadersBeginForInsert(&headers, nullptr, &position);
        void* out;
        HeadersInsertCopy(&headers, nullptr, &out, position, node->header);
    }
    node->rolledBack = 1;
}
EI_REPLACE_METHOD(0x00433F20, Connection::RollBack);

void Connection::AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key) {
    std::uint8_t* lb;
    AttachedLowerBound(&attached, nullptr, &lb, &object);
    if (lb == attached.header || reinterpret_cast<std::uintptr_t>(object) < *reinterpret_cast<std::uintptr_t*>(lb + 0x10)) {
        struct {
            CRefObject* key;
            std::uint32_t value;
        } pair{object, type};
        std::uint8_t* hint;
        AttachedIteratorCopy(&hint, nullptr, &lb);
        std::uint8_t* inserted;
        AttachedInsertHint(&attached, nullptr, &inserted, hint, &pair);
        lb = inserted;
    }
    std::uint16_t id = static_cast<std::uint16_t>(key);
    std::memcpy(lb + 0x14, &id, 2);
    lb[0x16] = static_cast<std::uint8_t>(type);
}
EI_REPLACE_METHOD(0x004352F0, Connection::AttachRecord);

bool Connection::SendUpdate(int budget) {
    bool complete = true;
    CollectChanges();
    BufferCursor c;
    c.Reset(nullptr, 0, 0);
    c.sink = nullptr;
    GameBuffer* out = SendBuffer();
    ei::VCall<void>(out, 6, 0);   // Seek(0)
    c.BeginWrite(out, budget + 0x400);

    *c.p = 0;   // packet kind: update
    std::uint8_t* payload = c.p + 1;
    std::uint16_t sequence = sendSequence++;
    c.p = payload;
    std::memcpy(c.p, &sequence, 2);
    c.p += 2;
    WriteAcks(&c.p);
    QueueHeader(budget - 0x10);

    // The reliable chunk at the front of the queue goes into the packet and into its history entry.
    const std::uint8_t* chunk = NextOf<HeaderNode>(Sentinel(&headers))->data;
    std::size_t chunkSize = static_cast<std::size_t>(chunk[2]) + 3;
    std::memcpy(c.p, chunk, chunkSize);
    c.p += chunkSize;
    void* inserted;
    HistoryInsert(&history, nullptr, &inserted, Sentinel(&history));
    HistoryNode* sent = LastOf<HistoryNode>(&history);
    sent->time[0] = clock[0];
    sent->time[1] = clock[1];
    sent->sequence = sequence;
    std::memcpy(sent->header, chunk, sizeof(sent->header));
    void* afterErase;
    HeadersErase(&headers, nullptr, &afterErase, NextOf<void>(Sentinel(&headers)));

    std::uint8_t* count = c.p++;
    *count = 0;
    for (int priority = 10; priority >= 0; --priority) {
        RbTree& map = priorities[priority];
        void* it = TreeBegin(map);
        while (it != map.header) {
            auto* entry = *reinterpret_cast<ReplicaEntry**>(static_cast<std::uint8_t*>(it) + 0x14);
            CRefObject* object = entry->object;
            bool alive;
            if (object && !(object->flags7_ & CRefObject::kDetached)) {
                alive = true;
                if (entry->version == *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(object) + 8)) {
                    TreeIncrement(&it);
                    continue;
                }
                if (entry->id == 0) {
                    entry->id = idPools[entry->type].Allocate();
                    std::uint32_t key = (static_cast<std::uint32_t>(entry->type) << 16) + entry->id;
                    BySentIdAt(bySentId, nullptr, &key)->Assign(reinterpret_cast<CRefObject*>(entry));
                }
                c.PutBits(1, 1);
                std::uint32_t id = entry->id;
                *c.p = entry->type;
                ++c.p;
                c.WriteBytes(&id, 2);
                WriteObject(entry->object, &c);
            } else {
                alive = false;
                if (entry->id == 0) {
                    TreeIncrement(&it);
                    continue;
                }
                if (entry->object) {
                    CRefObject* key = entry->object;
                    ByObjectErase(&byObject, nullptr, &key);
                    reinterpret_cast<RefPtr*>(&entry->object)->Assign(nullptr);
                    entry->version = 0;
                }
                if (entry->version != 0) {
                    TreeIncrement(&it);
                    continue;
                }
                c.PutBits(0, 1);
                std::uint32_t id = entry->id;
                *c.p = entry->type;
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
            void* added;
            ItemsInsert(&sent->items, nullptr, &added, sent->items);
            SentItem* item = sent->items->prev;
            ReplicaEntry* old = item->entry;
            item->entry = entry;
            ++reinterpret_cast<CRefObject*>(entry)->refs_;
            if (old) reinterpret_cast<CRefObject*>(old)->Release();
            if (entry->flags & 1) {
                if (alive) {
                    item->action = 1;
                    item->version = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(entry->object) + 8);
                } else {
                    item->action = 3;
                }
            } else if (alive) {
                entry->version = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(entry->object) + 8);
                item->action = 0;
                item->version = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(entry->object) + 8);
            } else {
                entry->version = 1;
                item->action = 2;
            }
            TreeIncrement(&it);
        }
    }
    c.End();

    if (SocketSend(&CurrentSession()->socket, nullptr, address, out)) return complete;
    // Not sent: undo what the history entry recorded and drop it.
    RollBack(reinterpret_cast<std::uint8_t*>(LastOf<HistoryNode>(&history)) + 8);
    void* last = Sentinel(&history);
    ListIteratorDecrement(&last);
    void* afterHistoryErase;
    HistoryErase(&history, nullptr, &afterHistoryErase, last);
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
    RecordNode* found;
    RecordsFind(&records, nullptr, &found, &key);
    SessionBase* session = CurrentSession();

    if (!created) {
        // Removal.
        if (found == records.header) {
            RecordAt(records, key)->version = version;
            return true;
        }
        if (found->version >= version) {
            LogLine(kSkipRemove);
            return true;
        }
        auto* slot = reinterpret_cast<RefPtr*>(&found->object);
        if (slot->ptr) {
            CRefObject* object = slot->ptr;
            AttachedErase(&attached, nullptr, &object);
            object = slot->ptr;
            session = CurrentSession();
            CallHandlers(session, 2, type, object);
        }
        if (slot->ptr) {
            slot->Destroy();
            slot->Share(nullptr);
        }
        found->version = version;
        return true;
    }

    if (found == records.header) {
        // A new object.
        RecordNode* node = RecordAt(records, key);
        CRefObject* object = reinterpret_cast<CRefObject*>(session->types.Create(type & 0xFF));
        if (!object) return false;
        RefPtrReset(reinterpret_cast<RefPtr*>(&node->object), nullptr, object);
        node->version = 0;
        AttachRecord(object, type, key);
        ReadObject(object, cursor);
        node->version = version;
        CallHandlers(CurrentSession(), 0, type, object);
        return true;
    }

    if (found->version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine(kSkipUpdate);
        RefPtr scratch;
        scratch.Share(reinterpret_cast<CRefObject*>(CurrentSession()->types.Create(type & 0xFF)));
        ReadObject(scratch.ptr, cursor);
        scratch.Destroy();
        return true;
    }

    auto* slot = reinterpret_cast<RefPtr*>(&found->object);
    if (!slot->ptr) {
        CRefObject* object = reinterpret_cast<CRefObject*>(CurrentSession()->types.Create(type & 0xFF));
        if (object) {
            RefPtrReset(slot, nullptr, object);
            found->version = 0;
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
    CallHandlers(CurrentSession(), found->version == 0 ? 0 : 1, type, object);
    found->version = version;
    return true;
}
EI_REPLACE_METHOD(0x00435F30, Connection::ReadRecord);

void Connection::Track(Replicated* item) {
    auto* object = static_cast<CRefObject*>(item->object);
    if (!object || (object->flags7_ & CRefObject::kDetached)) return;
    std::uint32_t objectVtable;
    std::memcpy(&objectVtable, object, 4);
    void* found[2];
    TypeFind(&CurrentSession()->types.byVtable, nullptr, found, &objectVtable);
    std::int32_t type = found[0] ? static_cast<std::int32_t>(static_cast<TypeNode*>(found[0])->value) : -1;

    // A new entry (vtable 0073BF4C) with no version and no id yet.
    auto* entry = static_cast<ReplicaEntry*>(gamecrt::operator_new(sizeof(ReplicaEntry)));
    entry->refHeader = 0;   // before the null check, as in the original
    if (entry) {
        std::uintptr_t base = 0x0073BB58;
        std::memcpy(entry, &base, 4);
        RefPtrRaw(reinterpret_cast<RefPtr*>(&entry->object), nullptr, nullptr);
        std::uintptr_t vt = 0x0073BF4C;
        std::memcpy(entry, &vt, 4);
        entry->version = 0;
        entry->id = 0;
    }

    void* lb;
    ByObjectLowerBound(&byObject, nullptr, &lb, &item->object);
    if (lb == byObject.header || reinterpret_cast<std::uintptr_t>(item->object) <
                                     *reinterpret_cast<std::uintptr_t*>(static_cast<std::uint8_t*>(lb) + 0x10)) {
        RefPtr none;
        RefPtrNull(&none);
        struct {
            void* key;
            RefPtr value;
        } pair;
        ByObjectMakePair(&pair, nullptr, &item->object, &none);
        void* hint;
        ByObjectIteratorCopy(&hint, nullptr, &lb);
        void* inserted;
        ByObjectInsertHint(&byObject, nullptr, &inserted, hint, &pair);
        lb = inserted;
        if (pair.value.ptr) pair.value.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    auto*& slot = *reinterpret_cast<CRefObject**>(static_cast<std::uint8_t*>(lb) + 0x14);
    CRefObject* old = slot;
    slot = reinterpret_cast<CRefObject*>(entry);
    if (entry) ++slot->refs_;
    if (old) old->Release();

    RbTree& map = priorities[static_cast<std::int8_t>(item->priority)];
    void* lb2;
    PriorityLowerBound(&map, nullptr, &lb2, reinterpret_cast<void* const*>(&entry));
    if (lb2 == map.header || reinterpret_cast<std::uintptr_t>(entry) <
                                 *reinterpret_cast<std::uintptr_t*>(static_cast<std::uint8_t*>(lb2) + 0x10)) {
        RefPtr none;
        RefPtrNull(&none);
        struct {
            void* key;
            RefPtr value;
        } pair;
        PriorityMakePair(&pair, nullptr, reinterpret_cast<void* const*>(&entry), &none);
        void* hint;
        PriorityIteratorCopy(&hint, nullptr, &lb2);
        void* inserted;
        PriorityInsertHint(&map, nullptr, &inserted, hint, &pair);
        lb2 = inserted;
        if (pair.value.ptr) pair.value.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    reinterpret_cast<RefPtr*>(static_cast<std::uint8_t*>(lb2) + 0x14)->Assign(reinterpret_cast<CRefObject*>(entry));
    entry->type = static_cast<std::uint8_t>(type);
    CRefObject* previous = entry->object;
    entry->object = static_cast<CRefObject*>(item->object);
    if (entry->object) ++entry->object->refs_;
    if (previous) previous->Release();
    entry->flags = static_cast<std::uint8_t>((entry->flags & 1) | (item->priority << 1));
    std::uint8_t reliable = reinterpret_cast<std::uint8_t*>(CurrentSession())[0x64 + type];
    entry->flags = static_cast<std::uint8_t>(((reliable ^ entry->flags) & 1) ^ entry->flags);
}
EI_REPLACE_METHOD(0x00434110, Connection::Track);

void Connection::CollectChanges() {
    ReplicatedVector& items = StaticReplicated();
    if (!(StaticReplicatedGuard() & 1)) {
        StaticReplicatedGuard() |= 1;
        char allocator = 0;
        ReplicatedConstruct(&items, nullptr, &allocator);
        GameAtExit(reinterpret_cast<void*>(kReplicatedDestroyAtExit));
    }
    ReplicatedErase(&items, nullptr, items.begin, items.end);
    ReplicatedVector* target = &items;
    ei::VCall<void>(CurrentSession(), 13, &target);
    if (items.begin != items.end) {
        int depth = 0;
        for (std::int32_t n = static_cast<std::int32_t>(items.end - items.begin); n != 1; n >>= 1) ++depth;
        ReplicatedSortLoop(items.begin, items.end, nullptr, depth * 2, target);
        ReplicatedSortFinish(items.begin, items.end, target);
    }

    // Merge the sorted list with byObject (both ordered by object pointer).
    Replicated* item = items.begin;
    Replicated* end = items.end;
    void* header = byObject.header;
    void* node = TreeBegin(byObject);
    auto entryAt = [](void* n) { return *reinterpret_cast<ReplicaEntry**>(static_cast<std::uint8_t*>(n) + 0x14); };
    if (item != end) {
        do {
            if (node == header) {
                for (; item < end; ++item) Track(item);
                return;
            }
            ReplicaEntry* entry = entryAt(node);
            auto listed = reinterpret_cast<std::uintptr_t>(item->object);
            auto tracked = reinterpret_cast<std::uintptr_t>(entry->object);
            if (listed == tracked) {
                TreeIncrement(&node);
                ++item;
            } else if (listed < tracked) {
                Track(item);
                ++item;
            } else {
                // Tracked but no longer listed: never sent -> forgotten; sent -> marked gone.
                void* gone = node;
                TreeIncrement(&node);
                entry = entryAt(gone);
                if (entry->id == 0) {
                    PriorityErase(&priorities[entry->flags >> 1], nullptr, reinterpret_cast<void* const*>(&entry));
                } else {
                    CRefObject* old = entry->object;
                    entry->object = nullptr;
                    if (old) old->Release();
                    entry->version = 0;
                }
                ByObjectEraseAt(&byObject, nullptr, gone);
            }
        } while (item != end);
    }
    while (node != header) {
        void* gone = node;
        TreeIncrement(&node);
        ReplicaEntry* entry = entryAt(gone);
        if (entry->id == 0) {
            PriorityErase(&priorities[entry->flags >> 1], nullptr, reinterpret_cast<void* const*>(&entry));
        } else {
            reinterpret_cast<RefPtr*>(&entry->object)->Assign(nullptr);
            entry->version = 0;
        }
        ByObjectEraseAt(&byObject, nullptr, gone);
    }
}
EI_REPLACE_METHOD(0x004346D0, Connection::CollectChanges);

} // namespace net
