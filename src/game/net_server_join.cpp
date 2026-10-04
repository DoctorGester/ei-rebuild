// The game server's handlers of the login message (0073BBF8, registered by the server constructor):
// a client joined (0041ADB0: a player record and a connection binding for it), its record changed
// (0041AD80) or it left (0041B0C0).
// See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall StringAssign(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, void* /*tag*/) EI_ORIGINAL(0x0042A4F0)
__declspec(naked) void __fastcall RefNullA(void* /*ref*/) EI_ORIGINAL(0x0041FF80)
__declspec(naked) void __fastcall RefNullB(void* /*ref*/) EI_ORIGINAL(0x0041FF70)
// Counted pointers from a raw pointer without a reference (the list insert adds one).
__declspec(naked) RefPtr* __fastcall PlayerRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428040)
__declspec(naked) RefPtr* __fastcall BindingRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428050)
// The players list (+0x2590) and the bindings list (+0x25A4): end, insert(pos, value), begin.
__declspec(naked) void** __fastcall PlayersEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420390)
__declspec(naked) void** __fastcall PlayersInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004223D0)
__declspec(naked) void** __fastcall PlayersBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420380)
__declspec(naked) void** __fastcall BindingsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004206E0)
__declspec(naked) void** __fastcall BindingsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00422BB0)
__declspec(naked) void** __fastcall BindingsEnd2(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00422BA0)
__declspec(naked) void** __fastcall BindingsBegin2(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00422B90)
__declspec(naked) void __fastcall CountNodes(int* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x00429110)
__declspec(naked) void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/) EI_ORIGINAL(0x005F8C90)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)
// erase(position) of the players and bindings lists, distance(first, last) (library).
__declspec(naked) void** __fastcall PlayersErase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x004293B0)
__declspec(naked) void** __fastcall BindingsErase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00429490)
__declspec(naked) void __fastcall CountPlayers(std::uint32_t* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x004290D0)

constexpr std::uintptr_t kPlayerVtable = 0x0073BC70;
constexpr std::uintptr_t kBindingVtable = 0x0073BCB8;
constexpr std::uintptr_t kTextTable = 0x007C7D50;

void Put32(void* p, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &v, 4); }
std::uint32_t Get32(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
void SetVtable(void* p, std::uintptr_t v) { std::memcpy(p, &v, 4); }
void* LastObject(void* list) {
    void** sentinel = *static_cast<void***>(list);
    return static_cast<void**>(sentinel[1])[2];
}
std::uint32_t& PlayerCounter() { return *reinterpret_cast<std::uint32_t*>(0x0079B60C); }
CRefObject* LocalClient() { return *reinterpret_cast<CRefObject**>(0x0079B5D8); }

} // namespace

// 0041ADB0: a client joined (`message`: name +0xC, second text +0x18, host flag byte +0x24, +0x28,
// +0x2C, binding slot +0x30). Appends a player record (vtable 0073BC70: the texts and values, the
// lowest free player number at +0x34, the next serial at +0x30, the server's +0x258C at +0x38) and
// a binding (vtable 0073BCB8: player and connection 0079B924) the message keeps. The player is the
// host when the message says so (with a local client) or when it is the only binding. Announces
// the player's name.
void __fastcall ServerOnJoin(ServerSession* self, Message* message) {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
    auto* msg = reinterpret_cast<std::uint8_t*>(message);

    auto* player = static_cast<std::uint8_t*>(gamecrt::operator_new(0x3C));
    Put32(player, 4, 0);
    if (player) {
        MessageConstructBase(reinterpret_cast<Message*>(player));
        StringConstruct(reinterpret_cast<StringRep*>(player + 0xC));
        StringConstruct(reinterpret_cast<StringRep*>(player + 0x18));
        SetVtable(player, kPlayerVtable);
        player[0x28] = 0;
        Put32(player, 0x24, 1);
        Put32(player, 0x2C, 0xFFFFFFFFu);
        Put32(player, 0x34, 0xFFFFFFFFu);
        Put32(player, 0x30, 0xFFFFFFFFu);
    }
    void* players = base + 0x2590;
    RefPtr ref;
    const RefPtr* value = PlayerRefFromRaw(&ref, player);
    void* end;
    PlayersEnd(players, nullptr, &end);
    void* inserted;
    PlayersInsert(players, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* record = static_cast<std::uint8_t*>(LastObject(players));

    auto* binding = static_cast<std::uint8_t*>(gamecrt::operator_new(0x14));
    Put32(binding, 4, 0);
    if (binding) {
        MessageConstructBase(reinterpret_cast<Message*>(binding));
        RefNullA(binding + 0xC);
        RefNullB(binding + 0x10);
        SetVtable(binding, kBindingVtable);
    }
    void* bindings = base + 0x25A4;
    value = BindingRefFromRaw(&ref, binding);
    BindingsEnd(bindings, nullptr, &end);
    BindingsInsert(bindings, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* bound = static_cast<std::uint8_t*>(LastObject(bindings));

    auto* name = reinterpret_cast<StringRep*>(record + 0xC);
    auto* fromName = reinterpret_cast<StringRep*>(msg + 0xC);
    if (fromName != name) StringAssign(name, nullptr, fromName->start, fromName->finish, name);
    Put32(record, 0x24, Get32(msg, 0x28));
    auto* text = reinterpret_cast<StringRep*>(record + 0x18);
    auto* fromText = reinterpret_cast<StringRep*>(msg + 0x18);
    if (fromText != text) StringAssign(text, nullptr, fromText->start, fromText->finish, name);
    Put32(record, 0x2C, Get32(msg, 0x2C));

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        void* it;
        PlayersBegin(players, nullptr, &it);
        bool used = false;
        while (it != *static_cast<void**>(players)) {
            auto* other = static_cast<std::uint8_t*>(static_cast<void**>(it)[2]);
            if (static_cast<int>(Get32(other, 0x34)) == number) {
                used = true;
                break;
            }
            it = *static_cast<void**>(it);
        }
        if (!used) break;
        ++number;
    }
    Put32(record, 0x34, static_cast<std::uint32_t>(number));
    Put32(record, 0x30, PlayerCounter());
    ++PlayerCounter();
    Put32(record, 0x38, Get32(base, 0x258C));
    reinterpret_cast<RefPtr*>(bound + 0xC)->Assign(reinterpret_cast<CRefObject*>(record));
    reinterpret_cast<RefPtr*>(bound + 0x10)->Assign(*reinterpret_cast<CRefObject**>(0x0079B924));
    reinterpret_cast<RefPtr*>(msg + 0x30)->Assign(reinterpret_cast<CRefObject*>(bound));

    CRefObject* client = LocalClient();
    bool host;
    if (client && !(client->flags7_ & CRefObject::kDetached)) {
        host = msg[0x24] != 0;
    } else {
        int count = 0;
        void* last;
        void* lastValue = *BindingsEnd2(bindings, nullptr, &last);
        void* first;
        void* firstValue = *BindingsBegin2(bindings, nullptr, &first);
        CountNodes(&count, nullptr, firstValue, lastValue);
        host = count == 1;
    }
    if (host) record[0x28] = 1;

    std::uint32_t announcement = 0;   // CString
    void* formatted = TextFormat(reinterpret_cast<void*>(kTextTable), nullptr, &announcement, 0, name->start);
    self->Announce(*static_cast<const char**>(formatted), nullptr);
    CStringDestroy(&announcement);
}
EI_REPLACE(0x0041ADB0, ServerOnJoin);

namespace {
bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}
void* At(const void* p, std::size_t offset) {
    void* v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
// Every node of `list` holding `value` erased (the next node taken first).
template <typename Erase>
void EraseValue(void* list, Erase erase, const void* value) {
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
} // namespace

// 0041AD80 (channel 1): the client's record changed: its player (through the binding at +0x30)
// takes the message's +0x28 and +0x2C at its +0x24 and +0x2C, and its version goes up.
void __fastcall ServerOnPlayerChanged(ServerSession*, Message* message) {
    Put32(At(At(message, 0x30), 0xC), 0x24, Get32(message, 0x28));
    Put32(At(At(message, 0x30), 0xC), 0x2C, Get32(message, 0x2C));
    void* player = At(At(message, 0x30), 0xC);
    Put32(player, 8, Get32(player, 8) + 1);
}
EI_REPLACE(0x0041AD80, ServerOnPlayerChanged);

// 0041B0C0 (channel 2): the client left. With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void __fastcall ServerOnPlayerGone(ServerSession* self, Message* message) {
    void* binding = At(message, 0x30);
    if (!Live(binding)) return;
    void* player = At(binding, 0xC);
    if (Live(player)) {
        std::uint8_t wasHost = static_cast<std::uint8_t*>(player)[0x28];
        EraseValue(&self->list_2590, PlayersErase, player);
        if (wasHost) {
            std::uint32_t count = 0;
            void** sentinel = static_cast<void**>(self->list_2590);
            CountPlayers(&count, nullptr, sentinel[0], sentinel);
            if (count > 0) {
                void* first = static_cast<void**>(static_cast<void**>(self->list_2590)[0])[2];
                static_cast<std::uint8_t*>(first)[0x28] = 1;
                first = static_cast<void**>(static_cast<void**>(self->list_2590)[0])[2];
                Put32(first, 8, Get32(first, 8) + 1);
            }
        }
    }
    EraseValue(&self->list_25A4, BindingsErase, At(message, 0x30));
}
EI_REPLACE(0x0041B0C0, ServerOnPlayerGone);

} // namespace net
