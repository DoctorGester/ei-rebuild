#pragma once

// The connection's STLport lists whose elements own something (S12e, docs/notes/standalone.md):
// the send history (list<SentPacket>, each packet owning a list<SentItem> of counted entries), the
// freed-id batches (list<vector<FreedId>>) and the reliable chunk lists (list<ReliableChunk>, plain
// bytes), with the element policies (stlp_list.h) their copy constructors, default inserts, erase,
// clear and ~list use; the session's clients find_if by peer address and the counted pointer's
// init. Registered in library_net_lists.cpp; checked in game by verify_library_net_lists.cpp until S21d.
// Thiscalls are __fastcall with an unused EDX.

#include "net_connection.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

namespace netlists {

// ---- list<SentItem> (a packet's items, 20-byte nodes from the pool) --------------------------------

// What the packet carried for one entry, the entry counted.
using SentRecord = net::SentItem;
using SentItems = stlp::list<SentRecord>;

// _M_create_node's copy (0043ABB0): the action and version, the entry with one more reference (its
// 16-bit count; a null entry stays null). Destroyed with the entry's release (0043A120). insert(pos)
// copies a default item: the original leaves action and version as its stack had them (every caller
// overwrites them); ours are zero.
struct SentRecordElement {
    static void Copy(SentRecord* to, const SentRecord& from) {
        to->action = from.action;
        to->version = from.version;
        to->entry = from.entry;
        if (from.entry) ++from.entry->refs_;
    }
    static void Destroy(SentRecord* p) {
        if (p->entry) p->entry->Release();
    }
    static void Default(SentRecord* p) {
        p->action = 0;
        p->version = 0;
        p->entry = nullptr;
    }
};

// ---- list<SentPacket> (the send history and the acknowledged packets; 0x120-byte nodes from
// operator new) -------------------------------------------------------------------------------------

using PacketRecord = net::SentPacket;
using SentPackets = stlp::list<PacketRecord>;

// _M_create_node's copy (0043ADE0): the time, the sequence (not the word after it), the items by
// the list's copy constructor (a sentinel, then a copy of each item), the header's 0x104 bytes and
// the rolled-back byte (not the three after it). Destroyed as 0043A1D0: the items cleared (each
// entry released, the node back to the pool), then the items' sentinel. insert(pos) copies a
// default packet (004397E0): an empty item list and rolledBack 0; the time, sequence and header
// were the original's stack bytes (the caller overwrites them), ours are zero.
struct PacketRecordElement {
    static void Copy(PacketRecord* to, const PacketRecord& from);
    static void Destroy(PacketRecord* p) { p->items.Release<SentRecordElement>(); }
    static void Default(PacketRecord* p);
};

// ---- list<vector<FreedId>> (the freed-id batches, 20-byte nodes from the pool) ---------------------

using FreedIds = net::FreedIds;
using FreedBatches = stlp::list<FreedIds>;

// _M_create_node's copy (0043AC40): vector's copy constructor (room for exactly size() ids, none
// when empty). Destroyed as 0043A150 (the storage back to the pool, by its capacity). insert(pos)
// copies an empty vector.
struct FreedIdsElement {
    static void Copy(FreedIds* to, const FreedIds& from) {
        to->CopyConstruct<stlp::PlainElement<net::FreedId>>(from);
    }
    static void Destroy(FreedIds* p) { p->FreeStorage(); }
    static void Default(FreedIds* p) { p->ConstructEmpty(); }
};

// ---- list<ReliableChunk> (the waiting and out-of-order chunks; 0x10C-byte nodes from operator new)

using Chunks = stlp::list<net::ReliableChunk>;

// The chunk's bytes copied. insert(pos) (00439C80) copies a default chunk: the original's stack
// bytes (the caller writes the sequence, size and data); ours are zero.
struct ChunkElement {
    static void Copy(net::ReliableChunk* to, const net::ReliableChunk& from) { *to = from; }
    static void Destroy(net::ReliableChunk*) {}
    static void Default(net::ReliableChunk* p) { *p = net::ReliableChunk{}; }
};

// ---- The members -----------------------------------------------------------------------------------

// list(const list& x) (00437930, 00437F60, 00438690; RET 4): a sentinel, then insert(end(),
// x.begin(), x.end()); returns self.
template <typename T, typename E>
stlp::list<T>* __fastcall CopyConstruct(stlp::list<T>* self, void*, const stlp::list<T>* from) {
    self->Construct();
    stlp::list<T>::template InsertRange<E>(self->head, from->First(), from->End());
    return self;
}

using SentItemAbi = stlp::ListAbi<SentRecord, SentRecordElement>;
using SentPacketAbi = stlp::ListAbi<PacketRecord, PacketRecordElement>;
using FreedBatchAbi = stlp::ListAbi<FreedIds, FreedIdsElement>;
using ChunkAbi = stlp::ListAbi<net::ReliableChunk, ChunkElement>;

// ---- The session's clients (list<Connection*>, counted) ---------------------------------------------

struct ClientsNode {
    ClientsNode* next;
    ClientsNode* prev;
    net::Connection* connection;
};
static_assert(sizeof(ClientsNode) == sizeof(net::ClientNode));

// find_if(first, last, same peer address) (0043D780 through 0043FB30; thiscall on out, RET 0xC): the
// first node whose connection's address has the 16 bytes of `address`, or last; returns out.
ClientsNode** __fastcall FindClientByAddress(ClientsNode** out, void*, ClientsNode* first, ClientsNode* last,
                                             const net::NetAddress* address);

// The clients' counted pointer made from a raw connection (0043D7D0; ECX the pointer, EDX the
// connection): stored without a reference; returns the pointer.
net::Connection** __fastcall InitClientRef(net::Connection** ref, net::Connection* connection);

} // namespace netlists
