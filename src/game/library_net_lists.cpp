// The connection's owning lists and the clients' find (library_net_lists.h), registered at the exe's
// addresses so the standalone image has them (S12e).
#include "library_net_lists.h"

#include "ei/hook.h"

#include <cstring>

namespace netlists {

void PacketRecordElement::Copy(PacketRecord* to, const PacketRecord& from) {
    to->time = from.time;
    to->sequence = from.sequence;
    to->items.Construct();
    SentItems::InsertRange<SentRecordElement>(to->items.head, from.items.First(), from.items.End());
    to->header = from.header;
    to->rolledBack = from.rolledBack;
}

void PacketRecordElement::Default(PacketRecord* p) {
    p->time = 0.0;
    p->sequence = 0;
    p->items.Construct();
    p->header = net::ReliableChunk{};
    p->rolledBack = 0;
}

ClientsNode** __fastcall FindClientByAddress(ClientsNode** out, void*, ClientsNode* first, ClientsNode* last,
                                             const net::NetAddress* address) {
    while (first != last && std::memcmp(&first->connection->address_, address, sizeof(net::NetAddress)) != 0)
        first = first->next;
    *out = first;
    return out;
}

net::Connection** __fastcall InitClientRef(net::Connection** ref, net::Connection* connection) {
    *ref = connection;
    return ref;
}

} // namespace netlists

using namespace netlists;

// list(const list&): the freed-id batches, the send history, the chunk lists (Connection::Clone).
EI_REPLACE(0x00437930, (CopyConstruct<FreedIds, FreedIdsElement>));
EI_REPLACE(0x00437F60, (CopyConstruct<PacketRecord, PacketRecordElement>));
EI_REPLACE(0x00438690, (CopyConstruct<net::ReliableChunk, ChunkElement>));

// insert(pos) of a default element: a packet's items, the freed-id batches, the history, the
// waiting chunks.
EI_REPLACE(0x00438F60, SentItemAbi::InsertDefault);
EI_REPLACE(0x00439000, FreedBatchAbi::InsertDefault);
EI_REPLACE(0x004397E0, SentPacketAbi::InsertDefault);
EI_REPLACE(0x00439C80, ChunkAbi::InsertDefault);

// erase(pos), clear() and ~list() of the history and the batches.
EI_REPLACE(0x00437F30, SentPacketAbi::Erase);
EI_REPLACE(0x004390C0, FreedBatchAbi::Erase);
EI_REPLACE(0x0043CA40, SentPacketAbi::Clear);
EI_REPLACE(0x0043BA20, FreedBatchAbi::Clear);
EI_REPLACE(0x004399C0, SentPacketAbi::Destruct);

// The session's clients.
EI_REPLACE(0x0043D780, FindClientByAddress);
EI_REPLACE(0x0043D7D0, InitClientRef);
