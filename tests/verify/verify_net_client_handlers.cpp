// Checks for net_client_handlers.cpp. All ours only (they change the client's lists, allocate and
// call the interface); each checks the state the handler leaves:
//  - the counted slots (+0x25EC, +0x25F0, +0x25F4, +0x2618) hold the message or are null;
//  - the lists hold the message after an add and no longer after a removal (for the files, also no
//    request for the file is left on +0x25E8);
//  - the trade handlers leave no trade, no party description and no trade records when they end a
//    trade; 0041E0A0 leaves a record for the pair; 0041E270 a description holding the binding's
//    player and the partner; 0041E3E0 no description.
#include "net_verify.h"

#include "../../src/game/net_messages.h"
#include "../../src/game/core_refcount.h"

#include <cstdint>

namespace net {
void* __fastcall ClientFindParty(ClientSession* self);
void __fastcall ClientRecordTrade(ClientSession* self, void*, const void* party);
void __fastcall ClientStartParty(ClientSession* self, void*, CRefObject* partner);
void __fastcall ClientDropParty(ClientSession* self);
void __fastcall ClientOnBinding(ClientSession* self, Message* message);
void __fastcall ClientOnBindingGone(ClientSession* self, Message* message);
void __fastcall ClientOnPlayer(ClientSession* self, Message* message);
void __fastcall ClientOnPlayerChanged(ClientSession* self, Message* message);
void __fastcall ClientOnPlayerGone(ClientSession* self, Message* message);
void __fastcall ClientOnLine(ClientSession* self, LineMessage* message);
void __fastcall ClientOnLineGone(ClientSession* self, Message* message);
void __fastcall ClientOnInfo(ClientSession* self, Message* message);
void __fastcall ClientOnInfoGone(ClientSession* self, Message* message);
void __fastcall ClientOnZone(ClientSession* self, ZoneMessage* message);
void __fastcall ClientOnZoneGone(ClientSession* self, ZoneMessage* message);
void __fastcall ClientOnMap(ClientSession* self, MapMessage* message);
void __fastcall ClientOnMapGone(ClientSession* self, Message* message);
void __fastcall ClientOnVariable(ClientSession* self, Message* message);
void __fastcall ClientOnVariableChanged(ClientSession* self, Message* message);
void __fastcall ClientOnVariableGone(ClientSession* self, Message* message);
void __fastcall ClientOnFile(ClientSession* self, FileMessage* message);
void __fastcall ClientOnFileGone(ClientSession* self, Message* message);
void __fastcall ClientOnFileChunk(ClientSession* self, FileChunkMessage* message);
void __fastcall ClientOnObjectGone(ClientSession* self, Message* message);
void __fastcall ClientOnAward(ClientSession* self, Message* message);
void __fastcall ClientOnParty(ClientSession* self, Message* message);
void __fastcall ClientOnPartyChanged(ClientSession* self, Message* message);
void __fastcall ClientOnPartyGone(ClientSession* self, Message* message);
void __fastcall ClientOnMember(ClientSession* self, Message* message);
void __fastcall ClientOnTrade(ClientSession* self, Message* message);
void __fastcall ClientOnTradeChanged(ClientSession* self, TradeMessage* message);
void __fastcall ClientOnTradeGone(ClientSession* self, Message* message);
}

namespace {
void* s_unused;

using net::BindingMessage;
using net::PartyMessage;

// A handler taking a message class, called with the message base.
template <typename T, void(__fastcall* Fn)(net::ClientSession*, T*)>
void __fastcall As(net::ClientSession* s, net::Message* m) { Fn(s, static_cast<T*>(m)); }

// Whether an STLport list of pointers (the sentinel in `list`) holds `value`.
bool Holds(void* list, const void* value) {
    void** sentinel = static_cast<void**>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (node[2] == value) return true;
    return false;
}
bool Empty(void* list) { return static_cast<void**>(list)[0] == list; }

using Handler = void(__fastcall*)(net::ClientSession*, net::Message*);

void Slot(const char* name, Handler handler, net::ClientSession* self, net::Message* message, CRefObject* const* slot, bool set) {
    handler(self, message);
    ei::verify::Check(name, set ? static_cast<void*>(message) : nullptr, static_cast<void*>(*slot), "");
}
void Added(const char* name, Handler handler, net::ClientSession* self, net::Message* message, void* list) {
    handler(self, message);
    ei::verify::Check(name, true, Holds(list, message), "");
}
void Removed(const char* name, Handler handler, net::ClientSession* self, net::Message* message, void* list) {
    handler(self, message);
    ei::verify::Check(name, false, Holds(list, message), "");
}

void __fastcall BindingVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnBinding", net::ClientOnBinding, s, m, &s->binding_, true); }
void __fastcall BindingGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnBindingGone", net::ClientOnBindingGone, s, m, &s->binding_, false); }
void __fastcall InfoVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnInfo", net::ClientOnInfo, s, m, &s->info_, true); }
void __fastcall InfoGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnInfoGone", net::ClientOnInfoGone, s, m, &s->info_, false); }
void __fastcall MapVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnMap", As<net::MapMessage, net::ClientOnMap>, s, m, &s->map_, true); }
void __fastcall MapGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnMapGone", net::ClientOnMapGone, s, m, &s->map_, false); }

void __fastcall PlayerVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnPlayer", net::ClientOnPlayer, s, m, s->players_); }
void __fastcall PlayerGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnPlayerGone", net::ClientOnPlayerGone, s, m, s->players_); }
void __fastcall LineVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnLine", As<net::LineMessage, net::ClientOnLine>, s, m, s->lines_); }
void __fastcall LineGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnLineGone", net::ClientOnLineGone, s, m, s->lines_); }
void __fastcall ZoneVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnZone", As<net::ZoneMessage, net::ClientOnZone>, s, m, s->zones_); }
void __fastcall ZoneGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnZoneGone", As<net::ZoneMessage, net::ClientOnZoneGone>, s, m, s->zones_); }
void __fastcall VariableVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnVariable", net::ClientOnVariable, s, m, s->variables_); }
void __fastcall VariableGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnVariableGone", net::ClientOnVariableGone, s, m, s->variables_); }
void __fastcall ObjectGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnObjectGone", net::ClientOnObjectGone, s, m, s->chunks_); }
void __fastcall AwardVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnAward", net::ClientOnAward, s, m, s->awards_); }
void __fastcall PartyVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnParty", net::ClientOnParty, s, m, s->offers_); }
void __fastcall PartyGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnPartyGone", net::ClientOnPartyGone, s, m, s->offers_); }
void __fastcall FileVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnFile", As<net::FileMessage, net::ClientOnFile>, s, m, s->files_); }
void __fastcall FileGoneVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnFileGone(s, m);
    bool request = false;
    void** sentinel = static_cast<void**>(s->fileRequests_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (static_cast<net::FileRequestMessage*>(node[2])->FileRef().ptr == m) request = true;
    ei::verify::Check("ClientOnFileGone", false, Holds(s->files_, m) || request, "");
}

// 0041ECC0 (A-E2): the chunk is on the list; once the list holds every chunk of a named file, no
// request for the file is left. Only a multiplayer client that is sent a map file reaches it (and
// only in the vanilla launch: Rebirth patches the original); no scenario does.
void __fastcall FileChunkVerify(net::ClientSession* s, net::Message* m) {
    auto* chunk = static_cast<net::FileChunkMessage*>(m);
    auto* file = static_cast<net::FileMessage*>(chunk->file_.ptr);
    net::ClientOnFileChunk(s, chunk);
    int count = 0;
    void** chunks = static_cast<void**>(s->chunks_);
    for (void** node = static_cast<void**>(chunks[0]); node != chunks; node = static_cast<void**>(node[0]))
        if (static_cast<net::FileChunkMessage*>(node[2])->file_.ptr == file) ++count;
    bool request = false;
    void** requests = static_cast<void**>(s->fileRequests_);
    for (void** node = static_cast<void**>(requests[0]); node != requests; node = static_cast<void**>(node[0]))
        if (static_cast<net::FileRequestMessage*>(node[2])->FileRef().ptr == file) request = true;
    const bool complete = file->name_.finish != file->name_.start && count == file->chunkCount_;
    ei::verify::Check("ClientOnFileChunk", true, Holds(s->chunks_, m) && !(complete && request), "%d of %u chunks, request %d", count,
                      static_cast<unsigned>(file->chunkCount_), request);
}

// Handlers that only notify.
void __fastcall PlayerChangedVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnPlayerChanged(s, m);
    ei::verify::Check("ClientOnPlayerChanged", true, true, "");
}
void __fastcall VariableChangedVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnVariableChanged(s, m);
    ei::verify::Check("ClientOnVariableChanged", true, true, "");
}
void __fastcall PartyChangedVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnPartyChanged(s, m);
    ei::verify::Check("ClientOnPartyChanged", true, true, "");
}
void __fastcall MemberVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnMember(s, m);
    ei::verify::Check("ClientOnMember", true, true, "");
}

// The trade.
void __fastcall TradeVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnTrade(s, m);
    ei::verify::Check("ClientOnTrade", true, s->trade_ == m && Empty(s->tradeRecords_), "");
}
void __fastcall TradeChangedVerify(net::ClientSession* s, net::Message* m) {
    auto* trade = static_cast<net::TradeMessage*>(m);
    bool ends = trade->done_ && s->trade_ == m;
    net::ClientOnTradeChanged(s, trade);
    if (ends)
        ei::verify::Check("ClientOnTradeChanged", true,
                          !s->trade_ && !s->party_.ptr && Empty(s->tradeRecords_), "");
    else
        ei::verify::Check("ClientOnTradeChanged", true, true, "");
}
void __fastcall TradeGoneVerify(net::ClientSession* s, net::Message* m) {
    bool ends = s->trade_ == m;
    net::ClientOnTradeGone(s, m);
    if (ends)
        ei::verify::Check("ClientOnTradeGone", true,
                          !s->trade_ && !s->party_.ptr && Empty(s->tradeRecords_), "");
    else
        ei::verify::Check("ClientOnTradeGone", true, true, "");
}
void __fastcall RecordTradeVerify(net::ClientSession* s, void*, const void* party) {
    bool records = net::ClientFindParty(s) && s->party_.ptr;
    net::ClientRecordTrade(s, nullptr, party);
    bool found = !records;
    if (records) {
        auto* ours = static_cast<PartyMessage*>(s->party_.ptr);
        std::uint32_t a = ours->acceptedRevision_, b = ours->revision_;
        net::TradeRecordNode* sentinel = s->tradeRecords_;
        for (net::TradeRecordNode* node = sentinel->next; node != sentinel; node = node->next)
            if (node->acceptedRevision == a && node->revision == b) found = true;
    }
    ei::verify::Check("ClientRecordTrade", true, found, "");
}
void __fastcall StartPartyVerify(net::ClientSession* s, void*, CRefObject* partner) {
    auto live = [](const void* o) {
        return o && !(static_cast<const CRefObject*>(o)->flags7_ & CRefObject::kDetached);
    };
    auto* binding = static_cast<BindingMessage*>(s->binding_);
    bool starts = live(binding) && live(binding->PlayerRef().ptr);
    net::ClientStartParty(s, nullptr, partner);
    auto* party = static_cast<PartyMessage*>(s->party_.ptr);
    bool ok = !starts || (party && party->player_.ptr == binding->PlayerRef().ptr && party->partner_.ptr == partner);
    ei::verify::Check("ClientStartParty", true, ok, "");
}
void __fastcall DropPartyVerify(net::ClientSession* s) {
    net::ClientDropParty(s);
    ei::verify::Check("ClientDropParty", static_cast<void*>(nullptr), static_cast<void*>(s->party_.ptr), "");
}
} // namespace

EI_VERIFY(0x0041E0A0, RecordTradeVerify, 0, s_unused);
EI_VERIFY(0x0041E270, StartPartyVerify, 0, s_unused);
EI_VERIFY(0x0041E3E0, DropPartyVerify, 0, s_unused);
EI_VERIFY(0x0041E430, BindingVerify, 0, s_unused);
EI_VERIFY(0x0041E460, BindingGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E490, PlayerVerify, 0, s_unused);
EI_VERIFY(0x0041E510, PlayerChangedVerify, 0, s_unused);
EI_VERIFY(0x0041E520, PlayerGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E5D0, LineVerify, 0, s_unused);
EI_VERIFY(0x0041E690, LineGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E730, InfoVerify, 0, s_unused);
EI_VERIFY(0x0041E760, InfoGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E790, ZoneVerify, 0, s_unused);
EI_VERIFY(0x0041E850, ZoneGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E900, MapVerify, 0, s_unused);
EI_VERIFY(0x0041E960, MapGoneVerify, 0, s_unused);
EI_VERIFY(0x0041E990, VariableVerify, 0, s_unused);
EI_VERIFY(0x0041EA10, VariableChangedVerify, 0, s_unused);
EI_VERIFY(0x0041EA20, VariableGoneVerify, 0, s_unused);
EI_VERIFY(0x0041EAC0, FileVerify, 0, s_unused);
EI_VERIFY(0x0041EBF0, FileGoneVerify, 0, s_unused);
EI_VERIFY(0x0041ECC0, FileChunkVerify, 0, s_unused);
EI_VERIFY(0x0041EF10, ObjectGoneVerify, 0, s_unused);
EI_VERIFY(0x0041EF90, AwardVerify, 0, s_unused);
EI_VERIFY(0x0041EFF0, PartyVerify, 0, s_unused);
EI_VERIFY(0x0041F090, PartyChangedVerify, 0, s_unused);
EI_VERIFY(0x0041F0D0, PartyGoneVerify, 0, s_unused);
EI_VERIFY(0x0041F150, MemberVerify, 0, s_unused);
EI_VERIFY(0x0041F180, MemberVerify, 0, s_unused);
EI_VERIFY(0x0041F1B0, MemberVerify, 0, s_unused);
EI_VERIFY(0x0041F1E0, TradeVerify, 0, s_unused);
EI_VERIFY(0x0041F270, TradeChangedVerify, 0, s_unused);
EI_VERIFY(0x0041F3A0, TradeGoneVerify, 0, s_unused);
