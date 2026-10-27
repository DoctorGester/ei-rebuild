#pragma once

// The game's STLport list<T> instantiations over game records (the plain-data and reference
// holder ones are in src/lib/stlp_list.cpp): an element policy per record (how _M_create_node
// copies one, how erase / clear destroy one, what insert(pos)'s T() is), each over the record's
// constructors and destructors, and the ListAbi adapters they are registered with
// (record_lists.cpp). Node sizes read from each instantiation's node creation.

#include "chunk_archive.h"
#include "core_streams.h"
#include "ground_print.h"
#include "model.h"
#include "net_classes.h"
#include "net_link.h"
#include "party.h"
#include "record_lifetime.h"
#include "ui_screens.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <memory>
#include <new>

namespace recordlist {

// The chunk archive's stack (ChunkArchive::Stack).
using ChunkRef = ChunkArchive::Ref;
using ChunkRefElement = ChunkArchive::RefElement;

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

// The link queues' memory streams (QueueNode, 48-byte nodes). The copy constructs a
// memory stream, sizes it as the source (SetSize) and calls its slot 1 (Read) with the source's
// buffer: the game reads the new stream's fresh block into the source rather than writing the
// source's bytes, so both hold the new block's bytes. Destroyed by the destructor; T()
// is an empty stream.
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

// CLinkFigureLoader's archives (LinkArchiveNode, 24-byte nodes): pair<string, CMMIOResource*>.
// The string copied into a block of its length + 1; T() is an empty string with room for
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
// copies (blood, footprints, scorch marks) are the classes' copy
// constructors (GroundPrint's, then their own members).
struct BloodPrintElement {
    static void Copy(BloodPrint* to, const BloodPrint& from) { new (to) BloodPrint(from); }
};
struct FootprintElement {
    static void Copy(Footprint* to, const Footprint& from) { new (to) Footprint(from); }
};
struct ScorchMarkElement {
    static void Copy(GroundPrint* to, const GroundPrint& from) { new (to) GroundPrint(from); }
};

// The titles' script lines (CCreditsScreen::lines_, 24-byte nodes): the flag byte, the
// speed and both CStrings copied.
struct TitleLineElement {
    static void Copy(TitleLine* to, const TitleLine& from) {
        to->scroll = from.scroll;
        to->speed = from.speed;
        std::construct_at(&to->text, from.text);
        std::construct_at(&to->style, from.style);
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
