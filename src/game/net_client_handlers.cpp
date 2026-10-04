// The game client's message handlers (registered by the constructor 0041BC40, __fastcall with the
// client in ECX and the message in EDX) and the three party-screen entry points around them
// (0041E0A0, 0041E270, 0041E3E0). Each keeps the replicated objects the server sends in one of the
// client's lists and tells the interface or the game flow. 0041ECC0 (the 0073BD30 handler for
// channel 0) is patched by Rebirth and stays original. See docs/notes/unmapped_410_470.md
// ("Eighth round").
#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);   // refptr_copies.cpp

namespace net {

void* __fastcall ClientFindParty(ClientSession* self);      // net_client_party.cpp
void __fastcall ClientApplyParty(ClientSession* self);      // net_client_party.cpp
void __fastcall ClientCommitParty(ClientSession* self);     // net_client_party.cpp

namespace {

using EndFn = void** (__fastcall*)(void*, void*, void**);
using InsertFn = void** (__fastcall*)(void*, void*, void**, void*, const void*);
using EraseFn = void** (__fastcall*)(void*, void*, void**, void*);

// The STLport lists of the client (library): end / insert / erase of each.
__declspec(naked) void** __fastcall PlayersEnd(void*, void*, void**) EI_ORIGINAL(0x00423950)
__declspec(naked) void** __fastcall PlayersInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00423960)
__declspec(naked) void** __fastcall PlayersErase(void*, void*, void**, void*) EI_ORIGINAL(0x00429550)
__declspec(naked) void** __fastcall LinesEnd(void*, void*, void**) EI_ORIGINAL(0x00423B40)
__declspec(naked) void** __fastcall LinesInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00423B50)
__declspec(naked) void** __fastcall LinesErase(void*, void*, void**, void*) EI_ORIGINAL(0x00429590)
__declspec(naked) void** __fastcall ZonesEnd(void*, void*, void**) EI_ORIGINAL(0x004242E0)
__declspec(naked) void** __fastcall ZonesInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x004242F0)
__declspec(naked) void** __fastcall ZonesErase(void*, void*, void**, void*) EI_ORIGINAL(0x00429690)
__declspec(naked) void** __fastcall VariablesEnd(void*, void*, void**) EI_ORIGINAL(0x004244D0)
__declspec(naked) void** __fastcall VariablesInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x004244E0)
__declspec(naked) void** __fastcall VariablesErase(void*, void*, void**, void*) EI_ORIGINAL(0x004296D0)
__declspec(naked) void** __fastcall FilesEnd(void*, void*, void**) EI_ORIGINAL(0x00423D30)
__declspec(naked) void** __fastcall FilesInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00423D40)
__declspec(naked) void** __fastcall FilesErase(void*, void*, void**, void*) EI_ORIGINAL(0x004295D0)
__declspec(naked) void** __fastcall RequestsEnd(void*, void*, void**) EI_ORIGINAL(0x00420CF0)
__declspec(naked) void** __fastcall RequestsInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00424100)
__declspec(naked) void** __fastcall RequestsErase(void*, void*, void**, void*) EI_ORIGINAL(0x00429650)
__declspec(naked) void** __fastcall ObjectsErase(void*, void*, void**, void*) EI_ORIGINAL(0x00429610)
__declspec(naked) void** __fastcall AwardsEnd(void*, void*, void**) EI_ORIGINAL(0x004210E0)
__declspec(naked) void** __fastcall AwardsInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00424720)
__declspec(naked) void** __fastcall PartiesEnd(void*, void*, void**) EI_ORIGINAL(0x004208A0)
__declspec(naked) void** __fastcall PartiesInsert(void*, void*, void**, void*, const void*) EI_ORIGINAL(0x00423190)
__declspec(naked) void** __fastcall PartiesErase(void*, void*, void**, void*) EI_ORIGINAL(0x004294D0)
// The trade records (+0x2614: 0x160-byte nodes {next, prev, a, b, three parties}).
__declspec(naked) void** __fastcall TradesInsertDefault(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00424900)
__declspec(naked) void __fastcall TradeRecordDestroy(void* /*record*/) EI_ORIGINAL(0x00424DB0)
// Parties (00661050 class, outside this range).
__declspec(naked) void __fastcall PartyCopyFrom(void* /*party*/, void*, const void* /*from*/) EI_ORIGINAL(0x00661300)
__declspec(naked) void __fastcall PartyReset(void* /*party*/, void*, int /*a*/, int /*b*/) EI_ORIGINAL(0x00661700)
// The interface's text table (0x7C7D50) and the game flow (0x7AFE20), outside this range.
__declspec(naked) void __fastcall UiPlayersChanged(void* /*table*/, void*, int /*added*/) EI_ORIGINAL(0x005F82D0)
__declspec(naked) void __fastcall UiChatLine(void* /*table*/, void*, const char* /*text*/, std::uint32_t /*number*/) EI_ORIGINAL(0x005F82F0)
__declspec(naked) void __fastcall UiTradeDone(void* /*table*/, void*, void* /*partyB*/, void* /*partyA*/, void* /*partyC*/) EI_ORIGINAL(0x005F8310)
__declspec(naked) void __fastcall FlowZoneOffered(void* /*flow*/, void*, const char* /*zone*/) EI_ORIGINAL(0x0057FF40)
__declspec(naked) void __fastcall FlowZoneWithdrawn(void* /*flow*/, void*, const char* /*zone*/) EI_ORIGINAL(0x00580040)
__declspec(naked) void __fastcall FlowVariablesChanged(void* /*flow*/) EI_ORIGINAL(0x005800C0)
__declspec(naked) bool __fastcall FlowHasFile(void* /*flow*/, void*, const char* /*name*/) EI_ORIGINAL(0x005812F0)
__declspec(naked) void __fastcall FlowLoadZoneQuests(void* /*flow*/, void*, const char* /*zone*/) EI_ORIGINAL(0x005809D0)
__declspec(naked) void __fastcall FlowLoadMap(void* /*flow*/, void*, const char* /*zone*/) EI_ORIGINAL(0x005810A0)
// Message members.
__declspec(naked) void __fastcall RefNullB(void* /*ref*/) EI_ORIGINAL(0x0041FF70)
__declspec(naked) void __fastcall RefNullA(void* /*ref*/) EI_ORIGINAL(0x0041FF80)
__declspec(naked) void __fastcall RefVectorConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0041FFF0)

constexpr std::uintptr_t kTextTable = 0x007C7D50;
constexpr std::uintptr_t kFlow = 0x007AFE20;
constexpr std::uintptr_t kRequestVtable = 0x0073BC58;
constexpr std::uintptr_t kPartyVtable = 0x0073BDD0;

void* TextTable() { return reinterpret_cast<void*>(kTextTable); }
void* Flow() { return reinterpret_cast<void*>(kFlow); }
template <typename T>
T& F(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

void Append(void* list, EndFn end, InsertFn insert, const void* value) {
    void* position;
    end(list, nullptr, &position);
    void* inserted;
    insert(list, nullptr, &inserted, position, value);
}
// Every node holding `value` erased (the sentinel read once, the next node taken first).
void RemoveValue(void* list, EraseFn erase, const void* value) {
    void** sentinel = *static_cast<void***>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (node[2] == value) {
            void* erased;
            erase(list, nullptr, &erased, node);
        }
        node = next;
    }
}
// A list given the message through a holder without a reference.
void AppendRaw(void* list, EndFn end, InsertFn insert, Message* message) {
    RefPtr raw;
    Append(list, end, insert, RefPtrFromRaw(&raw, reinterpret_cast<CRefObject*>(message)));
}
void RemoveRaw(void* list, EraseFn erase, Message* message) {
    RefPtr raw;
    RemoveValue(list, erase, RefPtrFromRaw(&raw, reinterpret_cast<CRefObject*>(message))->ptr);
}

// Counted pointers as the handlers inline them.
void AddRef(CRefObject* object) {
    if (object) ++object->refs_;
}
void ReleaseRef(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) ei::VCall<void>(object, 0, 1);
}
void AssignRef(void* slot, CRefObject* object) {
    auto*& holder = *static_cast<CRefObject**>(slot);
    CRefObject* previous = holder;
    holder = object;
    if (object) ++object->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) ei::VCall<void>(previous, 0, 1);
    }
}
void ClearRef(void* slot) {
    auto*& holder = *static_cast<CRefObject**>(slot);
    CRefObject* previous = holder;
    holder = nullptr;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) ei::VCall<void>(previous, 0, 1);
    }
}
// A list appended with a counted holder of the message that is released afterwards.
void AppendCounted(void* list, EndFn end, InsertFn insert, Message* message) {
    auto* object = reinterpret_cast<CRefObject*>(message);
    RefPtr local;
    local.ptr = object;
    AddRef(object);
    Append(list, end, insert, &local);
    ReleaseRef(local.ptr);
}
void RemoveCounted(void* list, EraseFn erase, Message* message) {
    auto* object = reinterpret_cast<CRefObject*>(message);
    AddRef(object);
    RemoveValue(list, erase, object);
    ReleaseRef(object);
}

// The owner's release of the party description (+0x260C) as the handlers inline it, then null.
void DropParty(ClientSession* self) {
    auto* state = static_cast<CRefObject*>(self->unknown_260C);
    if (!state) return;
    if (state->refs_ != 1) {
        state->flags7_ |= CRefObject::kDetached;
        ++state->refs_;
        ei::VCall<int>(state, 2);
        --state->refs_;
    }
    auto* held = static_cast<CRefObject*>(self->unknown_260C);
    --held->refs_;
    if (held->refs_ == 0 && held) ei::VCall<void>(held, 0, 1);
    self->unknown_260C = nullptr;
}

// The trade records emptied inline: each record destroyed, its node freed.
void ClearTradeRecords(ClientSession* self) {
    void* first = static_cast<void**>(self->list_2614)[0];
    for (void** node = static_cast<void**>(first); node != self->list_2614;) {
        void** current = node;
        node = static_cast<void**>(node[0]);
        TradeRecordDestroy(current + 2);
        if (current) gamecrt::operator_delete(current);
    }
    static_cast<void**>(self->list_2614)[0] = self->list_2614;
    static_cast<void**>(self->list_2614)[1] = self->list_2614;
}

const char* Text(Message* message, std::size_t offset) { return F<const char*>(message, offset); }

} // namespace

// --- The party-screen entry points -------------------------------------------------------------

// 0041E0A0: records the trade the paired party description stands for: our description is marked
// changed with the other's +0x24 at +0x20, and unless a record {+0x20, +0x24} exists one is added
// with copies of the two parties (+0x2620, +0x2690) and `party`.
void __fastcall ClientRecordTrade(ClientSession* self, void*, const void* party) {
    auto* remote = static_cast<std::uint8_t*>(ClientFindParty(self));
    if (!remote) return;
    if (!self->unknown_260C) return;
    F<std::uint8_t>(self->unknown_260C, 0x1C) = 1;
    F<std::uint32_t>(self->unknown_260C, 0x20) = F<std::uint32_t>(remote, 0x24);
    ++F<std::uint32_t>(self->unknown_260C, 8);
    std::uint32_t a = F<std::uint32_t>(self->unknown_260C, 0x20);
    std::uint32_t b = F<std::uint32_t>(self->unknown_260C, 0x24);
    void** sentinel = static_cast<void**>(self->list_2614);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (F<std::uint32_t>(node, 0xC) == b && F<std::uint32_t>(node, 8) == a) return;
    void* inserted;
    TradesInsertDefault(&self->list_2614, nullptr, &inserted, self->list_2614);
    void* last = static_cast<void**>(self->list_2614)[1];   // the node just added
    auto* record = static_cast<std::uint8_t*>(last) + 8;
    F<std::uint32_t>(record, 0) = F<std::uint32_t>(self->unknown_260C, 0x20);
    F<std::uint32_t>(record, 4) = F<std::uint32_t>(self->unknown_260C, 0x24);
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    PartyCopyFrom(record + 8, nullptr, base + 0x2620);
    PartyCopyFrom(record + 0x78, nullptr, base + 0x2690);
    PartyCopyFrom(record + 0xE8, nullptr, party);
}
EI_REPLACE(0x0041E0A0, ClientRecordTrade);

// 0041E270: starts a party description (0073BDD0) when the binding (+0x25F0) and its player are
// live: kept at +0x260C, the player at its +0x28 and `partner` at its +0x2C.
void __fastcall ClientStartParty(ClientSession* self, void*, CRefObject* partner) {
    auto* binding = static_cast<std::uint8_t*>(self->ref_25F0);
    if (!Live(binding)) return;
    if (!Live(F<void*>(binding, 0xC))) return;
    auto* party = static_cast<std::uint8_t*>(gamecrt::operator_new(0x34));
    F<std::uint32_t>(party, 4) = 0;
    if (party) {
        RefValueConstructBase(party);
        F<std::uintptr_t>(party, 0) = kMessageVtable;
        F<std::uint32_t>(party, 8) = 1;
        std::uint8_t allocator = 0;
        RefVectorConstruct(party + 0xC, nullptr, &allocator);
        F<std::uint32_t>(party, 0x28) = 0;
        RefNullA(party + 0x2C);
        F<std::uintptr_t>(party, 0) = kPartyVtable;
        F<std::uint32_t>(party, 0x20) = 0;
        F<std::uint32_t>(party, 0x24) = 1;
        party[0x1C] = 0;
        F<std::uint32_t>(party, 0x18) = 0;
        party[0x30] = 0;
    }
    auto* holder = reinterpret_cast<RefPtr*>(&self->unknown_260C);
    if (reinterpret_cast<CRefObject*>(party) != holder->ptr) {
        if (CRefObject* old = holder->ptr) {
            if (old->refs_ != 1) {
                old->flags7_ |= CRefObject::kDetached;
                old->Shutdown();
            }
            holder->ptr->Release();
        }
        holder->Share(reinterpret_cast<CRefObject*>(party));
    }
    AssignRef(static_cast<std::uint8_t*>(self->unknown_260C) + 0x28, F<CRefObject*>(self->ref_25F0, 0xC));
    AssignRef(static_cast<std::uint8_t*>(self->unknown_260C) + 0x2C, partner);
}
EI_REPLACE(0x0041E270, ClientStartParty);

// 0041E3E0: drops the party description.
void __fastcall ClientDropParty(ClientSession* self) { DropParty(self); }
EI_REPLACE(0x0041E3E0, ClientDropParty);

// --- Handlers ----------------------------------------------------------------------------------

// The binding of this client (0073BCB8): kept at +0x25F0 (0041E430), dropped (0041E460).
void __fastcall ClientOnBinding(ClientSession* self, Message* message) { AssignRef(&self->ref_25F0, reinterpret_cast<CRefObject*>(message)); }
void __fastcall ClientOnBindingGone(ClientSession* self, Message*) { ClearRef(&self->ref_25F0); }
// The players (0073BC70) on +0x25D8, with the interface's player list refreshed.
void __fastcall ClientOnPlayer(ClientSession* self, Message* message) {   // 0041E490
    AppendRaw(&self->list_25D8, PlayersEnd, PlayersInsert, message);
    UiPlayersChanged(TextTable(), nullptr, 1);
}
void __fastcall ClientOnPlayerChanged(ClientSession*, Message*) { UiPlayersChanged(TextTable(), nullptr, 0); }   // 0041E510
void __fastcall ClientOnPlayerGone(ClientSession* self, Message* message) {   // 0041E520
    RemoveCounted(&self->list_25D8, PlayersErase, message);
    UiPlayersChanged(TextTable(), nullptr, 1);
}
// The chat lines (0073BC88) on +0x25DC, each shown with its number.
void __fastcall ClientOnLine(ClientSession* self, Message* message) {   // 0041E5D0
    AppendCounted(&self->list_25DC, LinesEnd, LinesInsert, message);
    UiChatLine(TextTable(), nullptr, Text(message, 0x10), F<std::uint32_t>(message, 0xC));
}
void __fastcall ClientOnLineGone(ClientSession* self, Message* message) { RemoveCounted(&self->list_25DC, LinesErase, message); }   // 0041E690
// The 0073BCA0 record kept at +0x25EC.
void __fastcall ClientOnInfo(ClientSession* self, Message* message) { AssignRef(&self->ref_25EC, reinterpret_cast<CRefObject*>(message)); }   // 0041E730
void __fastcall ClientOnInfoGone(ClientSession* self, Message*) { ClearRef(&self->ref_25EC); }   // 0041E760
// The zones offered (0073BCD0) on +0x25F8, passed to the game flow.
void __fastcall ClientOnZone(ClientSession* self, Message* message) {   // 0041E790
    AppendCounted(&self->list_25F8, ZonesEnd, ZonesInsert, message);
    FlowZoneOffered(Flow(), nullptr, Text(message, 0xC));
}
void __fastcall ClientOnZoneGone(ClientSession* self, Message* message) {   // 0041E850
    RemoveCounted(&self->list_25F8, ZonesErase, message);
    FlowZoneWithdrawn(Flow(), nullptr, Text(message, 0xC));
}
// The current map (0073BD00) at +0x25F4; without a server of our own the flow loads it.
void __fastcall ClientOnMap(ClientSession* self, Message* message) {   // 0041E900
    AssignRef(&self->ref_25F4, reinterpret_cast<CRefObject*>(message));
    auto* server = *reinterpret_cast<CRefObject**>(0x0079B5DC);
    if (server && !(server->flags7_ & CRefObject::kDetached)) return;
    FlowLoadZoneQuests(Flow(), nullptr, Text(message, 0xC));
    FlowLoadMap(Flow(), nullptr, Text(message, 0xC));
}
void __fastcall ClientOnMapGone(ClientSession* self, Message*) { ClearRef(&self->ref_25F4); }   // 0041E960
// The script variables (0073BCE8) on +0x25FC; the flow is told of every change.
void __fastcall ClientOnVariable(ClientSession* self, Message* message) {   // 0041E990
    AppendRaw(&self->list_25FC, VariablesEnd, VariablesInsert, message);
    FlowVariablesChanged(Flow());
}
void __fastcall ClientOnVariableChanged(ClientSession*, Message*) { FlowVariablesChanged(Flow()); }   // 0041EA10
void __fastcall ClientOnVariableGone(ClientSession* self, Message* message) {   // 0041EA20
    RemoveRaw(&self->list_25FC, VariablesErase, message);
    FlowVariablesChanged(Flow());
}
// The shared files (0073BD18) on +0x25E0; for a file the flow lacks, a request (0073BC58 holding
// the file) goes on +0x25E8.
void __fastcall ClientOnFile(ClientSession* self, Message* message) {   // 0041EAC0
    AppendRaw(&self->list_25E0, FilesEnd, FilesInsert, message);
    if (FlowHasFile(Flow(), nullptr, Text(message, 0x14))) return;
    auto* request = static_cast<std::uint8_t*>(gamecrt::operator_new(0x14));
    F<std::uint32_t>(request, 4) = 0;
    if (request) {
        RefValueConstructBase(request);
        F<std::uintptr_t>(request, 0) = kMessageVtable;
        F<std::uint32_t>(request, 8) = 1;
        F<std::uint32_t>(request, 0xC) = 0;
        RefNullB(request + 0x10);
        F<std::uintptr_t>(request, 0) = kRequestVtable;
    }
    AssignRef(request + 0xC, reinterpret_cast<CRefObject*>(message));
    AppendRaw(&self->list_25E8, RequestsEnd, RequestsInsert, reinterpret_cast<Message*>(request));
}
void __fastcall ClientOnFileGone(ClientSession* self, Message* message) {   // 0041EBF0
    RemoveRaw(&self->list_25E0, FilesErase, message);
    void* requests = &self->list_25E8;
    void** sentinel = static_cast<void**>(self->list_25E8);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (F<Message*>(node[2], 0xC) == message) {
            void* erased;
            RequestsErase(requests, nullptr, &erased, node);
        }
        node = next;
    }
}
// The file chunks (0073BD30) on +0x25E4 (the channel 0 handler 0041ECC0, which stores them, is Rebirth's).
void __fastcall ClientOnObjectGone(ClientSession* self, Message* message) { RemoveRaw(&self->list_25E4, ObjectsErase, message); }   // 0041EF10
// The awards (0073BD48) on +0x2608 (applied by the tick).
void __fastcall ClientOnAward(ClientSession* self, Message* message) { AppendRaw(&self->list_2608, AwardsEnd, AwardsInsert, message); }   // 0041EF90
// The party descriptions (0073BDD0) on +0x2610; the one paired with ours rebuilds the party.
void __fastcall ClientOnParty(ClientSession* self, Message* message) {   // 0041EFF0
    AppendRaw(&self->list_2610, PartiesEnd, PartiesInsert, message);
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyChanged(ClientSession* self, Message* message) {   // 0041F090
    if (ClientFindParty(self) != message) return;
    ClientCommitParty(self);
    ClientApplyParty(self);
}
void __fastcall ClientOnPartyGone(ClientSession* self, Message* message) { RemoveRaw(&self->list_2610, PartiesErase, message); }   // 0041F0D0
// Party members (0073BD78), every channel: the description committed and the party rebuilt.
void __fastcall ClientOnMember(ClientSession* self, Message*) {   // 0041F150 / 0041F180 / 0041F1B0
    ClientCommitParty(self);
    ClientApplyParty(self);
}
// The trade (0073BD60) at +0x2618: a new one empties the trade records and resets both parties.
void __fastcall ClientOnTrade(ClientSession* self, Message* message) {   // 0041F1E0
    AssignRef(&self->unknown_2618, reinterpret_cast<CRefObject*>(message));
    ClearTradeRecords(self);
    PartyReset(&self->records[0], nullptr, 1, 0);
    PartyReset(&self->records[1], nullptr, 1, 0);
}
// A finished trade (done, and ours): the record of the two sides' values is shown, then the trade,
// the party description and the records are dropped.
void __fastcall ClientOnTradeChanged(ClientSession* self, Message* message) {   // 0041F270
    if (!F<std::uint8_t>(message, 0x1C) || message != self->unknown_2618) return;
    std::uint32_t a, b;
    if (F<void*>(message, 0x14) == self->unknown_260C) {
        b = F<std::uint32_t>(message, 0x20);
        a = F<std::uint32_t>(message, 0x24);
    } else {
        b = F<std::uint32_t>(message, 0x24);
        a = F<std::uint32_t>(message, 0x20);
    }
    void** sentinel = static_cast<void**>(self->list_2614);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        if (F<std::uint32_t>(node, 0xC) == b && F<std::uint32_t>(node, 8) == a) {
            auto* record = reinterpret_cast<std::uint8_t*>(node) + 8;
            UiTradeDone(TextTable(), nullptr, record + 0x78, record + 8, record + 0xE8);
            break;
        }
    }
    ClearRef(&self->unknown_2618);
    DropParty(self);
    ClearTradeRecords(self);
}
void __fastcall ClientOnTradeGone(ClientSession* self, Message* message) {   // 0041F3A0
    if (self->unknown_2618 != message) return;
    ClearRef(&self->unknown_2618);
    DropParty(self);
    ClearTradeRecords(self);
}

EI_REPLACE(0x0041E430, ClientOnBinding);
EI_REPLACE(0x0041E460, ClientOnBindingGone);
EI_REPLACE(0x0041E490, ClientOnPlayer);
EI_REPLACE(0x0041E510, ClientOnPlayerChanged);
EI_REPLACE(0x0041E520, ClientOnPlayerGone);
EI_REPLACE(0x0041E5D0, ClientOnLine);
EI_REPLACE(0x0041E690, ClientOnLineGone);
EI_REPLACE(0x0041E730, ClientOnInfo);
EI_REPLACE(0x0041E760, ClientOnInfoGone);
EI_REPLACE(0x0041E790, ClientOnZone);
EI_REPLACE(0x0041E850, ClientOnZoneGone);
EI_REPLACE(0x0041E900, ClientOnMap);
EI_REPLACE(0x0041E960, ClientOnMapGone);
EI_REPLACE(0x0041E990, ClientOnVariable);
EI_REPLACE(0x0041EA10, ClientOnVariableChanged);
EI_REPLACE(0x0041EA20, ClientOnVariableGone);
EI_REPLACE(0x0041EAC0, ClientOnFile);
EI_REPLACE(0x0041EBF0, ClientOnFileGone);
EI_REPLACE(0x0041EF10, ClientOnObjectGone);
EI_REPLACE(0x0041EF90, ClientOnAward);
EI_REPLACE(0x0041EFF0, ClientOnParty);
EI_REPLACE(0x0041F090, ClientOnPartyChanged);
EI_REPLACE(0x0041F0D0, ClientOnPartyGone);
EI_REPLACE(0x0041F150, ClientOnMember);
EI_REPLACE(0x0041F180, ClientOnMember);
EI_REPLACE(0x0041F1B0, ClientOnMember);
EI_REPLACE(0x0041F1E0, ClientOnTrade);
EI_REPLACE(0x0041F270, ClientOnTradeChanged);
EI_REPLACE(0x0041F3A0, ClientOnTradeGone);

} // namespace net
