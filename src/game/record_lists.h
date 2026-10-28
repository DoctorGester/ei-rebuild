#pragma once

// The game's STLport list<T> instantiations over game records: an element policy per record (how
// _M_create_node copies one, how erase / clear destroy one, what insert(pos)'s T() is), each over the
// record's constructors and destructors; callers pass them to stlp::list<T>'s members (`Insert<TradeElement>`).
// Node sizes read from each instantiation's node creation.

#include "chunk_archive.h"
#include "core_streams.h"
#include "ground_print.h"
#include "model.h"
#include "net_classes.h"
#include "net_link.h"
#include "party.h"
#include "ui_screens.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <memory>
#include <new>

namespace recordlist {


// The client's finished trades (ClientSession::tradeRecords_, TradeRecordNode; 0x160-byte nodes from
// operator new): two revisions and three parties. Copied member by member (CParty's copy
// constructor), destroyed last party first (CParty's destructor); T() constructs
// the parties and leaves the revisions as the stack held them.
using net::TradeRecord;
struct TradeElement {
    static void Copy(TradeRecord* to, const TradeRecord& from) {
        to->acceptedRevision = from.acceptedRevision;
        to->revision = from.revision;
        // CParty's copy constructor takes its source non-const (it saves it into a stream).
        TradeRecord& source = const_cast<TradeRecord&>(from);
        for (int i = 0; i < 3; ++i) new (to->parties[i].Get()) CParty(*source.parties[i].Get());
    }
    static void Destroy(TradeRecord* p) {
        for (int i = 3; i-- > 0;) p->parties[i]->~CParty();
    }
    static void Default(TradeRecord* p) {
        for (net::PartyStorage& party : p->parties) new (party.Get()) CParty();
    }
};

// The link queues' memory streams (QueueNode, 48-byte nodes). The copy constructs a memory stream, sizes it
// as the source (SetSize) and calls its slot 1 (Read) with the source's buffer: the game reads the new
// stream's fresh block into the source rather than writing the source's bytes, so both hold the new block's
// bytes. Destroyed by the destructor; T() is an empty stream.
struct QueueStreamElement {
    static void Copy(CMemoryStream* to, const CMemoryStream& from) {
        new (to) CMemoryStream();
        to->SetSize(from.size_);
        to->Read(from.data_, to->size_);
    }
    static void Destroy(CMemoryStream* p) { p->~CMemoryStream(); }
    static void Default(CMemoryStream* p) { new (p) CMemoryStream(); }
};
static_assert(sizeof(stlp::list<CMemoryStream>::Node) == 0x30);

// CLinkFigureLoader's archives (LinkArchiveNode): pair<string, CMMIOResource*>.
// The string copied into a block of its length + 1; T() is an empty string with room for
// 8 bytes and the resource as the stack held it (AddArchive sets it).
struct LinkArchive {
    std::string name;
    CMappedResource* resource;
};
static_assert(sizeof(stlp::list<LinkArchive>::Node) == sizeof(LinkArchiveNode));
struct LinkArchiveElement {
    static void Copy(LinkArchive* to, const LinkArchive& from) {
        std::construct_at(&to->name, from.name);
        to->resource = from.resource;
    }
    static void Destroy(LinkArchive* p) { std::destroy_at(&p->name); }
    static void Default(LinkArchive* p) { std::construct_at(&p->name); }
};

} // namespace recordlist
