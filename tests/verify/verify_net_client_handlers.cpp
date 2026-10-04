// Checks for net_client_handlers.cpp. All ours only (they change the client's lists, allocate and
// call the interface); each checks the state the handler leaves:
//  - the counted slots (+0x25EC, +0x25F0, +0x25F4, +0x2618) hold the message or are null;
//  - the lists hold the message after an add and no longer after a removal (for the files, also no
//    request for the file is left on +0x25E8);
//  - the trade handlers leave no trade, no party description and no trade records when they end a
//    trade; 0041E0A0 leaves a record for the pair; 0041E270 a description holding the binding's
//    player and the partner; 0041E3E0 no description.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
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
void __fastcall ClientOnLine(ClientSession* self, Message* message);
void __fastcall ClientOnLineGone(ClientSession* self, Message* message);
void __fastcall ClientOnInfo(ClientSession* self, Message* message);
void __fastcall ClientOnInfoGone(ClientSession* self, Message* message);
void __fastcall ClientOnZone(ClientSession* self, Message* message);
void __fastcall ClientOnZoneGone(ClientSession* self, Message* message);
void __fastcall ClientOnMap(ClientSession* self, Message* message);
void __fastcall ClientOnMapGone(ClientSession* self, Message* message);
void __fastcall ClientOnVariable(ClientSession* self, Message* message);
void __fastcall ClientOnVariableChanged(ClientSession* self, Message* message);
void __fastcall ClientOnVariableGone(ClientSession* self, Message* message);
void __fastcall ClientOnFile(ClientSession* self, Message* message);
void __fastcall ClientOnFileGone(ClientSession* self, Message* message);
void __fastcall ClientOnObjectGone(ClientSession* self, Message* message);
void __fastcall ClientOnAward(ClientSession* self, Message* message);
void __fastcall ClientOnParty(ClientSession* self, Message* message);
void __fastcall ClientOnPartyChanged(ClientSession* self, Message* message);
void __fastcall ClientOnPartyGone(ClientSession* self, Message* message);
void __fastcall ClientOnMember(ClientSession* self, Message* message);
void __fastcall ClientOnTrade(ClientSession* self, Message* message);
void __fastcall ClientOnTradeChanged(ClientSession* self, Message* message);
void __fastcall ClientOnTradeGone(ClientSession* self, Message* message);
}

namespace {
void* s_unused;

std::uint32_t At(const void* p, std::size_t offset) {
    return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(p) + offset);
}

// Whether an STLport list of pointers (the sentinel in `list`) holds `value`.
bool Holds(void* list, const void* value) {
    void** sentinel = static_cast<void**>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (node[2] == value) return true;
    return false;
}
bool Empty(void* list) { return static_cast<void**>(list)[0] == list; }

using Handler = void(__fastcall*)(net::ClientSession*, net::Message*);

void Slot(const char* name, Handler handler, net::ClientSession* self, net::Message* message, void* const* slot, bool set) {
    handler(self, message);
    ei::verify::Check(name, set ? static_cast<void*>(message) : nullptr, *slot, "");
}
void Added(const char* name, Handler handler, net::ClientSession* self, net::Message* message, void* list) {
    handler(self, message);
    ei::verify::Check(name, true, Holds(list, message), "");
}
void Removed(const char* name, Handler handler, net::ClientSession* self, net::Message* message, void* list) {
    handler(self, message);
    ei::verify::Check(name, false, Holds(list, message), "");
}

void __fastcall BindingVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnBinding", net::ClientOnBinding, s, m, &s->ref_25F0, true); }
void __fastcall BindingGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnBindingGone", net::ClientOnBindingGone, s, m, &s->ref_25F0, false); }
void __fastcall InfoVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnInfo", net::ClientOnInfo, s, m, &s->ref_25EC, true); }
void __fastcall InfoGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnInfoGone", net::ClientOnInfoGone, s, m, &s->ref_25EC, false); }
void __fastcall MapVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnMap", net::ClientOnMap, s, m, &s->ref_25F4, true); }
void __fastcall MapGoneVerify(net::ClientSession* s, net::Message* m) { Slot("ClientOnMapGone", net::ClientOnMapGone, s, m, &s->ref_25F4, false); }

void __fastcall PlayerVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnPlayer", net::ClientOnPlayer, s, m, s->list_25D8); }
void __fastcall PlayerGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnPlayerGone", net::ClientOnPlayerGone, s, m, s->list_25D8); }
void __fastcall LineVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnLine", net::ClientOnLine, s, m, s->list_25DC); }
void __fastcall LineGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnLineGone", net::ClientOnLineGone, s, m, s->list_25DC); }
void __fastcall ZoneVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnZone", net::ClientOnZone, s, m, s->list_25F8); }
void __fastcall ZoneGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnZoneGone", net::ClientOnZoneGone, s, m, s->list_25F8); }
void __fastcall VariableVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnVariable", net::ClientOnVariable, s, m, s->list_25FC); }
void __fastcall VariableGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnVariableGone", net::ClientOnVariableGone, s, m, s->list_25FC); }
void __fastcall ObjectGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnObjectGone", net::ClientOnObjectGone, s, m, s->list_25E4); }
void __fastcall AwardVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnAward", net::ClientOnAward, s, m, s->list_2608); }
void __fastcall PartyVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnParty", net::ClientOnParty, s, m, s->list_2610); }
void __fastcall PartyGoneVerify(net::ClientSession* s, net::Message* m) { Removed("ClientOnPartyGone", net::ClientOnPartyGone, s, m, s->list_2610); }
void __fastcall FileVerify(net::ClientSession* s, net::Message* m) { Added("ClientOnFile", net::ClientOnFile, s, m, s->list_25E0); }
void __fastcall FileGoneVerify(net::ClientSession* s, net::Message* m) {
    net::ClientOnFileGone(s, m);
    bool request = false;
    void** sentinel = static_cast<void**>(s->list_25E8);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (At(node[2], 0xC) == reinterpret_cast<std::uintptr_t>(m)) request = true;
    ei::verify::Check("ClientOnFileGone", false, Holds(s->list_25E0, m) || request, "");
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
    ei::verify::Check("ClientOnTrade", true, s->unknown_2618 == m && Empty(s->list_2614), "");
}
void __fastcall TradeChangedVerify(net::ClientSession* s, net::Message* m) {
    bool ends = *(reinterpret_cast<std::uint8_t*>(m) + 0x1C) && s->unknown_2618 == m;
    net::ClientOnTradeChanged(s, m);
    if (ends)
        ei::verify::Check("ClientOnTradeChanged", true,
                          !s->unknown_2618 && !s->unknown_260C && Empty(s->list_2614), "");
    else
        ei::verify::Check("ClientOnTradeChanged", true, true, "");
}
void __fastcall TradeGoneVerify(net::ClientSession* s, net::Message* m) {
    bool ends = s->unknown_2618 == m;
    net::ClientOnTradeGone(s, m);
    if (ends)
        ei::verify::Check("ClientOnTradeGone", true,
                          !s->unknown_2618 && !s->unknown_260C && Empty(s->list_2614), "");
    else
        ei::verify::Check("ClientOnTradeGone", true, true, "");
}
void __fastcall RecordTradeVerify(net::ClientSession* s, void*, const void* party) {
    bool records = net::ClientFindParty(s) && s->unknown_260C;
    net::ClientRecordTrade(s, nullptr, party);
    bool found = !records;
    if (records) {
        std::uint32_t a = At(s->unknown_260C, 0x20), b = At(s->unknown_260C, 0x24);
        void** sentinel = static_cast<void**>(s->list_2614);
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
            if (At(node, 8) == a && At(node, 0xC) == b) found = true;
    }
    ei::verify::Check("ClientRecordTrade", true, found, "");
}
void __fastcall StartPartyVerify(net::ClientSession* s, void*, CRefObject* partner) {
    auto live = [](const void* o) {
        return o && !(static_cast<const CRefObject*>(o)->flags7_ & CRefObject::kDetached);
    };
    bool starts = live(s->ref_25F0) && live(reinterpret_cast<void*>(At(s->ref_25F0, 0xC)));
    net::ClientStartParty(s, nullptr, partner);
    bool ok = !starts || (s->unknown_260C && At(s->unknown_260C, 0x28) == At(s->ref_25F0, 0xC) &&
                          At(s->unknown_260C, 0x2C) == reinterpret_cast<std::uintptr_t>(partner));
    ei::verify::Check("ClientStartParty", true, ok, "");
}
void __fastcall DropPartyVerify(net::ClientSession* s) {
    net::ClientDropParty(s);
    ei::verify::Check("ClientDropParty", static_cast<void*>(nullptr), s->unknown_260C, "");
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
