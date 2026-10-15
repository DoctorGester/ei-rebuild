#pragma once

// The game's STLport list<T> instantiations over game records (S9a; the plain-data and reference
// holder ones are in src/lib/stlp_list.cpp): an element policy per record (how _M_create_node
// copies one, how erase / clear destroy one, what insert(pos)'s T() is), each over the record's
// ported constructors and destructors, and the ListAbi shims they are registered with
// (record_lists.cpp). Node sizes read from each instantiation's node creation. Checked by
// tests/verify/verify_record_lists.cpp.

#include "chunk_archive.h"
#include "core_streams.h"
#include "ground_print.h"
#include "model.h"
#include "net_classes.h"
#include "net_link.h"
#include "party.h"
#include "record_lifetime.h"
#include "ui_screens.h"

#include "ei/vcall.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <new>

namespace recordlist {

// The chunk archive's stack (ChunkArchive::StackNode, 24-byte nodes): plain records; T() is
// Ref::Reset's {0xFF, 0, 0, occurrence 1} (the three unused bytes are what the stack held).
using ChunkRef = ChunkArchive::Ref;
struct ChunkRefElement {
    static void Copy(ChunkRef* to, const ChunkRef& from) { *to = from; }
    static void Destroy(ChunkRef*) {}
    static void Default(ChunkRef* p) { p->Reset(); }
};
static_assert(sizeof(stlp::list<ChunkRef>::Node) == sizeof(ChunkArchive::StackNode));

// The client's finished trades (ClientSession::tradeRecords_, TradeRecordNode; 0x160-byte nodes from
// operator new): two revisions and three parties. Copied member by member (CParty's copy
// constructor, 006613C0), destroyed last party first (006611D0, as 00424DB0 does); T() constructs
// the parties (00661050) and leaves the revisions as the stack held them.
struct TradeRecord {
    std::uint32_t acceptedRevision;
    std::uint32_t revision;
    net::PartyStorage parties[3];
};
static_assert(sizeof(stlp::list<TradeRecord>::Node) == sizeof(net::TradeRecordNode));
struct TradeElement {
    static void Copy(TradeRecord* to, const TradeRecord& from) {
        to->acceptedRevision = from.acceptedRevision;
        to->revision = from.revision;
        // CParty's copy constructor takes its source non-const (it saves it into a stream).
        TradeRecord& source = const_cast<TradeRecord&>(from);
        for (int i = 0; i < 3; ++i) to->parties[i].Party()->CopyConstruct(source.parties[i].Party());
    }
    static void Destroy(TradeRecord* p) {
        for (int i = 3; i-- > 0;) p->parties[i].Party()->DestructBody();
    }
    static void Default(TradeRecord* p) {
        for (net::PartyStorage& party : p->parties) party.Party()->Construct();
    }
};

// The link queues' memory streams (QueueNode, 48-byte nodes). The copy (00479410) constructs a
// memory stream, sizes it as the source (SetSize) and calls its slot 1 (Read) with the source's
// buffer: the original reads the new stream's fresh block into the source rather than writing the
// source's bytes, so both hold the new block's bytes; then the queue's vtable. Destroyed through
// slot 0 with flags 0 (004790D0); T() is a queue stream (0066C4F0).
struct QueueStreamElement {
    static void Copy(CMemoryStream* to, const CMemoryStream& from) {
        to->Construct();
        to->SetSize(from.size_);
        to->Read(from.data_, to->size_);
        ei::SetVtable(to, netlink::kNodeStreamVtable);
    }
    static void Destroy(CMemoryStream* p) { static_cast<CStream*>(p)->~CStream(); }
    static void Default(CMemoryStream* p) { p->ConstructQueueStream(); }
};
static_assert(sizeof(stlp::list<CMemoryStream>::Node) == sizeof(netlink::QueueNode));

// CLinkFigureLoader's archives (LinkArchiveNode, 24-byte nodes): pair<string, CMMIOResource*>.
// The string copied into a block of its length + 1 (0042A2B0); T() is an empty string with room for
// 8 bytes and the resource as the stack held it (AddArchive sets it).
struct LinkArchive {
    stlp::string name;
    CMappedResource* resource;
};
static_assert(sizeof(stlp::list<LinkArchive>::Node) == sizeof(LinkArchiveNode));
struct LinkArchiveElement {
    static void Copy(LinkArchive* to, const LinkArchive& from) {
        new (&to->name) stlp::string(from.name);
        to->resource = from.resource;
    }
    static void Destroy(LinkArchive* p) { p->name.~string(); }
    static void Default(LinkArchive* p) { stlp::StringElement::Default(&p->name); }
};

// The client world's ground prints (0x94 / 0x9C / 0xA4-byte nodes from operator new): the node
// copies (0056F880 blood, 0056F940 footprints, 0056F9F0 scorch marks) start with GroundPrint's copy
// constructor (00571FC0), copy their own members and store their class's vtable.
struct BloodPrintElement {
    static void Copy(BloodPrint* to, const BloodPrint& from) {
        to->CopyConstruct(&from);
        to->spin_ = from.spin_;
        to->spreadTicks_ = from.spreadTicks_;
        to->kind_ = from.kind_;
        to->fresh_ = from.fresh_;
        ei::SetVtable(to, BloodPrint::kVtableAddress);
    }
};
struct FootprintElement {
    static void Copy(Footprint* to, const Footprint& from) {
        to->CopyConstruct(&from);
        to->scale_ = from.scale_;
        to->kind_ = from.kind_;
        ei::SetVtable(to, Footprint::kVtableAddress);
    }
};
struct ScorchMarkElement {
    static void Copy(GroundPrint* to, const GroundPrint& from) {
        to->CopyConstruct(&from);
        ei::SetVtable(to, GroundPrint::kVtableAddress);
    }
};

// The titles' script lines (CCreditsScreen::lines_, 24-byte nodes; 0062C0C0): the flag byte, the
// speed and both CStrings copied (007006E5).
struct TitleLineElement {
    static void Copy(TitleLine* to, const TitleLine& from) {
        to->scroll = from.scroll;
        to->speed = from.speed;
        new (&records::AsCString(to->text)) mfc::CString(records::AsCString(from.text));
        new (&records::AsCString(to->style)) mfc::CString(records::AsCString(from.style));
    }
};

using ChunkStack = stlp::ListAbi<ChunkRef, ChunkRefElement>;
using Trades = stlp::ListAbi<TradeRecord, TradeElement>;
using StreamQueue = stlp::ListAbi<CMemoryStream, QueueStreamElement>;
using LinkArchives = stlp::ListAbi<LinkArchive, LinkArchiveElement>;
using BloodPrints = stlp::ListAbi<BloodPrint, BloodPrintElement>;
using Footprints = stlp::ListAbi<Footprint, FootprintElement>;
using ScorchMarks = stlp::ListAbi<GroundPrint, ScorchMarkElement>;
using TitleLines = stlp::ListAbi<TitleLine, TitleLineElement>;

} // namespace recordlist
