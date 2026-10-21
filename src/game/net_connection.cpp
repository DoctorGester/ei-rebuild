// Connections of the network sessions: object replication. See net_connection.h.
#include "net_coder.h"
#include "net_connection.h"
#include "net_connection_trees.h"
#include "library_net_lists.h"
#include "at_exit_functions.h"
#include "game_strings.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_tree_abi.h"

#include <cstring>
#include "originals_core.h"
#include "originals_library.h"
#include "stl_sorts.h"
#include "game_data.h"

RefPtr* __fastcall RefPtrNull(RefPtr* /*p*/);
namespace net {

namespace {

using ObjectMap = stlp::map<CRefObject*, RefPtr>;
using SentIdMap = stlp::map<std::int32_t, RefPtr>;
using AttachedMap = stlp::map<CRefObject*, AttachedId>;
using EntryMap = Connection::EntryMap;
using RecordMap = Connection::RecordMap;

// --- STLport container instantiations (library_net_lists.h, net_connection_trees.h) -------------
// The history's, the chunk lists' and a packet's items' element policies.
using netlists::ChunkElement;
using netlists::PacketRecordElement;
using netlists::SentRecordElement;
using Chunks = stlp::list<ReliableChunk>;
using Packets = stlp::list<SentPacket>;
using Items = stlp::list<SentItem>;
// The static vector of 004346D0: constructor, erase of a range (00439DD0), introsort loop and final
// insertion sort by object, atexit.
// 0079B8D0 / 0079B8DC: CollectChanges's static vector of {object, priority} and its guard.
ReplicatedVector& StaticReplicated() { return ei::GameData<ReplicatedVector>(0x0079B8D0); }
std::uint8_t& StaticReplicatedGuard() { return ei::GameData<std::uint8_t>(0x0079B8DC); }



// "text\n" and a flush to the network log, when there is one.
void LogLine(const char* text) {
    if (LogStream* log = NetLog()) log->Put(text)->PutChar('\n')->Flush();
}

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
    it = Node::From(stlp::rb::Next(it->Base()));
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

// records_.insert(hint, pair(key, Record())) as ReadRecord inlines it (the pair 0043A090, the
// insert 00439A70); returns the new node.
RecordMap::Node* InsertRecord(RecordMap& records, RecordMap::Node* hint, std::int32_t key) {
    Record empty;
    ::RefPtrNull(&empty.object);
    empty.version = 0;   // left uninitialised by the original; overwritten by every caller
    RecordMap::Value pair;
    trees::Records::MakeValue(&pair, nullptr, &key, &empty);
    RecordMap::Node* inserted;
    trees::Records::InsertHint(&records, nullptr, &inserted, hint, &pair);
    pair.second.object.Destroy();
    empty.object.Destroy();
    return inserted;
}

// The node for `key` in `records`, inserted when missing (the lower_bound + hinted insert idiom).
RecordMap::Node* RecordAt(RecordMap& records, std::int32_t key) {
    RecordMap::Node* lb;
    trees::Records::LowerBound(&records, nullptr, &lb, &key);
    if (lb == records.end() || key < lb->first) lb = InsertRecord(records, lb, key);
    return lb;
}

} // namespace

std::uint16_t IdPool::Allocate() {
    if (free.begin() == free.end()) return ++next;
    --free.Finish();
    return *free.end();
}
EI_REPLACE_METHOD(0x00435F10, IdPool::Allocate);

// 00433B10 (no Ghidra function; the client base's 00442A20 jumps to it with its first
// connection). Plain float maths (rewrite-guide rule 7): a silence within rounding of the limit
// may be judged one call apart from the original.
int Connection::Silent() const {
    float factor;
    std::memcpy(&factor, ei::GamePointer<const void>(0x0073BAA4), sizeof(factor));   // 3.0
    float limit = roundTrip_ * factor;
    return limit >= idleTime_ ? 0 : 1;   // a NaN is silent
}
EI_REPLACE_METHOD(0x00433B10, Connection::Silent);

void Connection::WriteAcks(std::uint8_t** p) {
    std::uint16_t base = static_cast<std::uint16_t>(lastReceived_);
    std::uint32_t mask = 0;
    std::int32_t* end = received_.end();
    std::int32_t target = static_cast<std::int32_t>(lastReceived_);
    for (int k = 1; k <= 32; ++k) {
        --target;
        std::int32_t* it = received_.begin();
        while (it != end && *it != target) ++it;
        if (it != end) mask |= 1u << (k - 1);
    }
    std::memcpy(*p, &base, 2);
    *p += 2;
    std::memcpy(*p, &mask, 4);
    *p += 4;
    // Forget sequences more than 32 behind (from the back, shifting the rest down).
    for (std::int32_t i = static_cast<std::int32_t>(received_.size()) - 1; i >= 0; --i) {
        std::int32_t* at = received_.begin() + i;
        if (static_cast<std::uint32_t>(*at) + 0x20 < lastReceived_) {
            for (std::int32_t* q = at; q + 1 < received_.end(); ++q) q[0] = q[1];
            --received_.Finish();
        }
    }
}
EI_REPLACE_METHOD(0x00434C20, Connection::WriteAcks);

void Connection::QueueHeader(int budget) {
    if (headers_.Empty()) {
        int size = budget < 0xFF ? budget : 0xFF;
        Chunks::InsertDefault<ChunkElement>(headers_.End());   // 00439C80
        std::uint8_t* chunk = headers_.End()->prev->data.bytes;
        int available = BufferedSize(&reliableOut_);
        if (size >= available) size = available;
        chunk[2] = static_cast<std::uint8_t>(size);
        std::memcpy(chunk, &headerSequence_, 2);
        std::memcpy(chunk + 3, reliableOut_.begin, static_cast<std::size_t>(size));
        int remaining = BufferedSize(&reliableOut_) - size;
        std::memmove(reliableOut_.begin, reliableOut_.begin + size, static_cast<std::size_t>(remaining));
        reliableOut_.cur = reliableOut_.begin;
        reliableOut_.high = reliableOut_.begin + remaining;
        if (reliableOut_.high > reliableOut_.end) static_cast<GameBuffer*>(&reliableOut_)->Grow(static_cast<std::uint32_t>(remaining));
        headerSequence_ = static_cast<std::uint16_t>(headerSequence_ + size);
        return;
    }
    // The oldest waiting chunk (sequence order with wrap-around) moves to the front.
    HeaderNode* head = headers_.End();
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
    Chunks::SpliceRange(headers_.First(), best, best->next);   // 00438770
}
EI_REPLACE_METHOD(0x00434990, Connection::QueueHeader);

void Connection::RollBack(SentPacket* packet) {
    if (packet->rolledBack) return;
    for (SentItemNode* node = packet->items.First(); node != packet->items.End(); node = node->next) {
        // Unreliable sends are forgotten (the entry is sent again); a reliable change only when
        // the entry still carries the version this packet sent.
        SentItem* item = &node->data;
        if (item->action == SentItem::kChanged) {
            if (item->entry->version_ == item->version) item->entry->version_ = 0;
        } else if (item->action == SentItem::kRemoved) {
            item->entry->version_ = 0;
        }
    }
    if (packet->header.Size() != 0) Chunks::Insert<ChunkElement>(headers_.End(), packet->header);   // 00439C50
    packet->rolledBack = 1;
}
EI_REPLACE_METHOD(0x00433F20, Connection::RollBack);

void Connection::AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key) {
    AttachedMap::Node* lb;
    trees::Attached::LowerBound(&attached_, nullptr, &lb, &object);
    if (lb == attached_.end() || reinterpret_cast<std::uintptr_t>(object) < reinterpret_cast<std::uintptr_t>(lb->first)) {
        // The pair holds the type dword in the id's place (both fields are written below).
        AttachedMap::Value pair{object, {static_cast<std::uint16_t>(type), static_cast<std::uint8_t>(type >> 16),
                                         static_cast<std::uint8_t>(type >> 24)}};
        AttachedMap::Node* inserted;
        trees::Attached::InsertHint(&attached_, nullptr, &inserted, lb, &pair);   // 00439B80
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
    const ReliableChunk& chunk = headers_.First()->data;
    std::size_t chunkSize = static_cast<std::size_t>(chunk.Size()) + 3;
    std::memcpy(c.p, chunk.bytes, chunkSize);
    c.p += chunkSize;
    Packets::InsertDefault<PacketRecordElement>(history_.End());   // 004397E0
    HistoryNode* sent = history_.End()->prev;
    std::memcpy(&sent->data.time, &clock_, sizeof(sent->data.time));
    sent->data.sequence = sequence;
    sent->data.header = chunk;
    Chunks::Erase<ChunkElement>(headers_.First());   // 00438660

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
                    std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                    trees::SentIds::Subscript(&bySentId_, nullptr, &key)->Assign(entry);   // 00437B90
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
                    trees::Objects::EraseKey(&byObject_, nullptr, &key);
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
            Items::InsertDefault<SentRecordElement>(sent->data.items.End());   // 00438F60
            SentItem* item = &sent->data.items.End()->prev->data;
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
    RollBack(&history_.End()->prev->data);
    Packets::Erase<PacketRecordElement>(history_.End()->prev);   // 00437F30
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
    trees::Records::Find(&records_, nullptr, &found, &key);
    SessionBase* session = CurrentSession();

    if (!created) {
        // Removal.
        if (found == records_.end()) {
            RecordAt(records_, key)->second.version = version;
            return true;
        }
        if (found->second.version >= version) {
            LogLine(game::nettexts::SkipRemove());
            return true;
        }
        RefPtr* slot = &found->second.object;
        if (slot->ptr) {
            CRefObject* object = slot->ptr;
            attached_.EraseKey<stlp::TreeElement<AttachedMap::Value>>(object);
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
        node->second.object.AssignObject(object);
        node->second.version = 0;
        AttachRecord(object, type, key);
        ReadObject(object, cursor);
        node->second.version = version;
        CallHandlers(CurrentSession(), 0, type, object);
        return true;
    }

    if (found->second.version >= version) {
        // Older than what was applied: read it into a throwaway object to skip its bytes.
        LogLine(game::nettexts::SkipUpdate());
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
            slot->AssignObject(object);
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
    TypeRegistry::ByVtable::Node* found = CurrentSession()->types_.byVtable.Find(objectVtable);   // 0042A810
    std::int32_t type = found ? static_cast<std::int32_t>(found->value) : -1;

    // A new entry (vtable 0073BF4C) with no version and no id yet.
    auto* entry = NewMessageMemory<ReplicaEntry>();
    if (entry) {
        ei::SetVtable(entry, CRefObject::kVtableAddress);
        entry->object_.FromRawCounted(nullptr);
        ei::SetVtable(entry, ReplicaEntry::kVtableAddress);
        entry->version_ = 0;
        entry->id_ = 0;
    }

    CRefObject* key = item->object;
    ObjectMap::Node* lb;
    trees::Objects::LowerBound(&byObject_, nullptr, &lb, &key);   // 0043C4B0
    if (lb == byObject_.end() || reinterpret_cast<std::uintptr_t>(item->object) < reinterpret_cast<std::uintptr_t>(lb->first)) {
        RefPtr none;
        ::RefPtrNull(&none);
        ObjectMap::Value pair;
        trees::Objects::MakeValue(&pair, nullptr, &key, &none);   // 0043A050
        ObjectMap::Node* inserted;
        trees::Objects::InsertHint(&byObject_, nullptr, &inserted, lb, &pair);   // 00439540
        lb = inserted;
        if (pair.second.ptr) pair.second.ptr->Release();
        if (none.ptr) none.ptr->Release();
    }
    CRefObject*& held = lb->second.ptr;
    CRefObject* old = held;
    held = entry;
    if (entry) ++held->refs_;
    if (old) old->Release();

    EntryMap& map = priorities_[static_cast<std::int8_t>(item->priority)];
    EntryMap::Node* lb2;
    trees::Entries::LowerBound(&map, nullptr, &lb2, &entry);
    if (lb2 == map.end() || reinterpret_cast<std::uintptr_t>(entry) < reinterpret_cast<std::uintptr_t>(lb2->first)) {
        RefPtr none;
        ::RefPtrNull(&none);
        EntryMap::Value pair;
        trees::Entries::MakeValue(&pair, nullptr, &entry, &none);   // 0043A070
        EntryMap::Node* inserted;
        trees::Entries::InsertHint(&map, nullptr, &inserted, lb2, &pair);   // 00439730
        lb2 = inserted;
        if (pair.second.ptr) pair.second.ptr->Release();
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
        items.ConstructEmpty();   // 00439E10
        GameAtExit(atexit400::DestroyGlobalVector);
    }
    items.EraseRange<stlp::PlainElement<Replicated>>(items.begin(), items.end());
    ReplicatedVector* target = &items;
    CurrentSession()->CollectReplicated(&target);
    if (items.begin() != items.end()) {
        int depth = 0;
        for (std::int32_t n = static_cast<std::int32_t>(items.size()); n != 1; n >>= 1) ++depth;
        stl_sorts::ReplicatedIntroSort(items.begin(), items.end(), nullptr, depth * 2, target);
        stl_sorts::ReplicatedFinalSort(items.begin(), items.end(), target);
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
                    trees::Entries::EraseKey(&priorities_[entry->flags_ >> 1], nullptr, &entry);
                } else {
                    CRefObject* old = entry->object_.ptr;
                    entry->object_.ptr = nullptr;
                    if (old) old->Release();
                    entry->version_ = 0;
                }
                trees::Objects::EraseAt(&byObject_, nullptr, gone);
            }
        } while (item != end);
    }
    while (node != header) {
        ObjectMap::Node* gone = node;
        Increment(node);
        ReplicaEntry* entry = entryAt(gone);
        if (entry->id_ == 0) {
            trees::Entries::EraseKey(&priorities_[entry->flags_ >> 1], nullptr, &entry);
        } else {
            entry->object_.Assign(nullptr);
            entry->version_ = 0;
        }
        trees::Objects::EraseAt(&byObject_, nullptr, gone);
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
        trees::Objects::Find(&byObject_, nullptr, &found, &object);
        if (found != byObject_.end()) {
            auto* entry = static_cast<ReplicaEntry*>(found->second.ptr);
            if (entry->id_ == 0) {
                entry->id_ = idPools_[entry->type_].Allocate();
                std::int32_t key = (static_cast<std::int32_t>(entry->type_) << 16) + entry->id_;
                AssignEntry(trees::SentIds::Subscript(&bySentId_, nullptr, &key), entry);
            }
            std::uint16_t id = entry->id_;
            std::uint8_t type = entry->type_;
            cursor->PutBit(0);
            cursor->WriteBytes(&type, 1);
            cursor->WriteBytes(&id, 2);
            return;
        }
        AttachedMap::Node* node;
        trees::Attached::Find(&attached_, nullptr, &node, &object);
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
        trees::SentIds::Find(&bySentId_, nullptr, &node, &key);   // 0043BF00
        if (node == bySentId_.end()) return nullptr;
        return static_cast<ReplicaEntry*>(node->second.ptr)->object_.ptr;
    }
    RecordMap::Node* found;
    trees::Records::Find(&records_, nullptr, &found, &key);
    if (found == records_.end()) {
        Record* record = trees::Records::Subscript(&records_, nullptr, &key);
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
    MessageFactory factory = CurrentSession()->types_.byType[typeKey];   // 00439EA0
    CRefObject* object = factory ? factory() : nullptr;
    if (!object) return nullptr;
    RefPtr* slot = &found->second.object;
    if (object != slot->ptr) {
        slot->Destroy();
        slot->Share(object);
    }
    found->second.version = 0;
    AttachedId* attachedId = trees::Attached::Subscript(&attached_, nullptr, &object);
    attachedId->id = static_cast<std::uint16_t>(key);
    attachedId->type = static_cast<std::uint8_t>(type);
    return object;
}
EI_REPLACE_METHOD(0x00435820, Connection::ReadObjectRef);

} // namespace net

namespace net {

namespace {
} // namespace

void Connection::Acknowledge(std::uint16_t sequence) {
    for (HistoryNode* node = history_.First(); node != history_.End(); node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            Packets::Erase<PacketRecordElement>(node);
            return;
        }
    }
    for (HistoryNode* node = acknowledged_.First(); node != acknowledged_.End(); node = node->next) {
        if (node->data.sequence == sequence) {
            Delivered(&node->data);
            Packets::Erase<PacketRecordElement>(node);
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
            log->Put(game::nettexts::TooOld())->PutInt(static_cast<int>(newest - sequence))->PutChar('\n')->Flush();
        return false;
    }
    if (!(newest > sequence)) newest = sequence;
    lastReceived_ = newest;
    for (std::int32_t* it = received_.begin(); it != received_.end(); ++it) {
        if (*it == static_cast<std::int32_t>(sequence)) {
            if (NetLog()) {
                stlp::string peer;
                const char* text = address_.ToString(&peer)->data();
                NetLog()->Put(game::nettexts::Duplicate())->Put(text)->PutChar('\n')->Flush();
            }
            return false;
        }
    }
    auto value = static_cast<std::int32_t>(sequence);
    if (received_.end() == received_.EndOfStorage()) {
        received_.InsertOverflow<stlp::PlainElement<std::int32_t>>(received_.end(), value, 1);   // 0043B850
    } else {
        if (received_.end()) *received_.end() = value;
        ++received_.Finish();
    }
    return true;
}
EI_REPLACE_METHOD(0x00434E50, Connection::Accept);

void Connection::ProcessHeaders() {
    bool progress;
    do {
        progress = false;
        HeaderNode* node = incoming_.First();
        if (node == incoming_.End()) return;
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
                node = Chunks::Erase<ChunkElement>(node);
                progress = true;
            } else {
                node = node->next;
            }
        } while (node != incoming_.End());
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
        Chunks::Insert<ChunkElement>(incoming_.End(), header);
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
            TypeRegistry::ByVtable::Node* found = session->types_.byVtable.Find(objectVtable);
            std::uint32_t type = found ? found->value : 0xFFFFFFFFu;
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
