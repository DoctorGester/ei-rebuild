// The game client session (vtable 0073BDEC, client base 0073BE30; pointer at 0079B5D8). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

// The handler registration templates (net_handler_registrars.cpp).
void __fastcall RegisterMapHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterVariableHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterFileHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterChunkHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterAwardHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterPartyHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterMemberHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterTradeHandler(SessionBase* self, void*, int channel, void* handler);

namespace {

// 004DA7F0: the growing byte buffer's constructor (net_coder.h, GameBuffer).
__declspec(naked) void* __fastcall GameBufferConstruct(void* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
// 00661050: the unit record builder's constructor (world.md).
__declspec(naked) UnitRecordBuilder* __fastcall UnitRecordBuilderConstruct(UnitRecordBuilder* /*b*/) EI_ORIGINAL(0x00661050)
// 0041F460: an empty STLport string (library).
__declspec(naked) StringRep* __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)

// STLport list constructors, list(const allocator&), one per element type (library).
__declspec(naked) void* __fastcall ListConstruct41F8B0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0041F8B0)
__declspec(naked) void* __fastcall ListConstruct41FB10(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0041FB10)
__declspec(naked) void* __fastcall ListConstruct423990(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423990)
__declspec(naked) void* __fastcall ListConstruct423B80(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423B80)
__declspec(naked) void* __fastcall ListConstruct423D70(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423D70)
__declspec(naked) void* __fastcall ListConstruct423F50(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423F50)
__declspec(naked) void* __fastcall ListConstruct424130(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00424130)
__declspec(naked) void* __fastcall ListConstruct424320(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00424320)
__declspec(naked) void* __fastcall ListConstruct424510(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00424510)
__declspec(naked) void* __fastcall ListConstruct420F10(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00420F10)
__declspec(naked) void* __fastcall ListConstruct424750(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00424750)
__declspec(naked) void* __fastcall ListConstruct4231C0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004231C0)
__declspec(naked) void* __fastcall ListConstruct4249E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004249E0)

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

void RegisterInline(ClientSession* client, std::size_t size, Message* (__fastcall* construct)(Message*),
                    int channel, std::uintptr_t handler) {
    client->AddHandlerFor(size, construct, channel, Handler(handler));
}

} // namespace

// 0041C420: the queries' constructor (an empty vector and a zero dword). Returns this.
QueryVector* __fastcall ClientVectorConstruct(QueryVector* self) {
    self->begin = nullptr;
    self->end = nullptr;
    self->eos = nullptr;
    self->extra = 0;
    return self;
}
EI_REPLACE(0x0041C420, ClientVectorConstruct);

// The client base's constructor is inlined: the session base, its lobby fields, an unbound socket.
ClientSession* ClientSession::Construct() {
    char allocator = 0;   // the lists' (empty) allocator argument
    SessionBase::Construct();
    host_ = NetAddress{};
    GameBufferConstruct(&joinData_);
    ListConstruct41F8B0(&servers_, nullptr, &allocator);
    ClientVectorConstruct(&queries_);
    ListConstruct41FB10(&waiting_, nullptr, &allocator);
    alwaysAsk_ = NetAddress{};
    ei::SetVtable(this, ClientBase::kVtableAddress);
    joinState_ = kNotJoined;
    joinId_ = 0;
    socket_.Open(-1);
    ListConstruct423990(&players_, nullptr, &allocator);
    ListConstruct423B80(&lines_, nullptr, &allocator);
    ListConstruct423D70(&files_, nullptr, &allocator);
    ListConstruct423F50(&chunks_, nullptr, &allocator);
    ListConstruct424130(&fileRequests_, nullptr, &allocator);
    info_ = nullptr;
    binding_ = nullptr;
    map_ = nullptr;
    ListConstruct424320(&zones_, nullptr, &allocator);
    ListConstruct424510(&variables_, nullptr, &allocator);
    player_.ptr = nullptr;
    ListConstruct420F10(&timers_, nullptr, &allocator);
    ListConstruct424750(&awards_, nullptr, &allocator);
    party_.ptr = nullptr;
    ListConstruct4231C0(&offers_, nullptr, &allocator);
    ListConstruct4249E0(&tradeRecords_, nullptr, &allocator);
    trade_ = nullptr;
    UnitRecordBuilderConstruct(&parties_[0]);
    UnitRecordBuilderConstruct(&parties_[1]);
    ei::SetVtable(this, kVtableAddress);

    // The local player's record (vtable 0073BBF8): two strings, +0x24 = 0, +0x28 = 4, +0x2C = -1.
    auto* record = static_cast<Message*>(gamecrt::operator_new(0x34));
    reinterpret_cast<std::uint32_t*>(record)[1] = 0;   // before the null check, as in the original
    if (record) {
        MessageConstructBase(record);
        auto* bytes = reinterpret_cast<std::uint8_t*>(record);
        StringConstruct(reinterpret_cast<StringRep*>(bytes + 0xC));
        StringConstruct(reinterpret_cast<StringRep*>(bytes + 0x18));
        *reinterpret_cast<std::uint32_t*>(bytes + 0x30) = 0;   // 0041FFE0
        ei::SetVtable(record, 0x0073BBF8);
        bytes[0x24] = 0;
        *reinterpret_cast<std::uint32_t*>(bytes + 0x28) = 4;
        *reinterpret_cast<std::uint32_t*>(bytes + 0x2C) = 0xFFFFFFFFu;
    }
    if (record != player_.ptr) {
        player_.Destroy();
        player_.Share(record);
    }

    magic_ = kSessionMagic;
    RegisterMessageTypes();

    RegisterInline(this, 0x14, ConstructMessage73BCB8, 0, 0x0041E430);
    RegisterInline(this, 0x14, ConstructMessage73BCB8, 2, 0x0041E460);
    RegisterInline(this, 0x3C, ConstructMessage73BC70, 0, 0x0041E490);
    RegisterInline(this, 0x3C, ConstructMessage73BC70, 1, 0x0041E510);
    RegisterInline(this, 0x3C, ConstructMessage73BC70, 2, 0x0041E520);
    RegisterInline(this, 0x20, ConstructMessage73BC88, 0, 0x0041E5D0);
    RegisterInline(this, 0x20, ConstructMessage73BC88, 2, 0x0041E690);
    RegisterInline(this, 0x24, ConstructMessage73BCA0, 0, 0x0041E730);
    RegisterInline(this, 0x24, ConstructMessage73BCA0, 2, 0x0041E760);
    RegisterInline(this, 0x18, ConstructMessage73BCD0, 0, 0x0041E790);
    RegisterInline(this, 0x18, ConstructMessage73BCD0, 2, 0x0041E850);

    RegisterMapHandler(this, nullptr, 0, Handler(0x0041E900));
    RegisterMapHandler(this, nullptr, 2, Handler(0x0041E960));
    RegisterVariableHandler(this, nullptr, 0, Handler(0x0041E990));
    RegisterVariableHandler(this, nullptr, 1, Handler(0x0041EA10));
    RegisterVariableHandler(this, nullptr, 2, Handler(0x0041EA20));
    RegisterFileHandler(this, nullptr, 0, Handler(0x0041EAC0));
    RegisterFileHandler(this, nullptr, 2, Handler(0x0041EBF0));
    RegisterChunkHandler(this, nullptr, 0, Handler(0x0041ECC0));   // patched by Rebirth: passed by address
    RegisterChunkHandler(this, nullptr, 2, Handler(0x0041EF10));
    RegisterAwardHandler(this, nullptr, 0, Handler(0x0041EF90));
    RegisterPartyHandler(this, nullptr, 0, Handler(0x0041EFF0));
    RegisterPartyHandler(this, nullptr, 1, Handler(0x0041F090));
    RegisterPartyHandler(this, nullptr, 2, Handler(0x0041F0D0));
    RegisterMemberHandler(this, nullptr, 0, Handler(0x0041F150));
    RegisterMemberHandler(this, nullptr, 1, Handler(0x0041F180));
    RegisterMemberHandler(this, nullptr, 2, Handler(0x0041F1B0));
    RegisterTradeHandler(this, nullptr, 0, Handler(0x0041F1E0));
    RegisterTradeHandler(this, nullptr, 1, Handler(0x0041F270));
    RegisterTradeHandler(this, nullptr, 2, Handler(0x0041F3A0));

    ei::VCall<void>(&parties_[0], 2);
    ei::VCall<void>(&parties_[1], 2);
    return this;
}
EI_REPLACE_METHOD(0x0041BC40, ClientSession::Construct);

// 0041D050 (slot 13). What the server receives from this client: the local player's record
// (+0x2600), the entries of +0x2604 (value at +0xC) and +0x25E8, and the group at +0x260C with its
// members (vector at +0xC). All priority 1.
void ClientSession::CollectReplicated(ReplicatedVector** out) {
    ClientSession* self = this;
    PushReplicated(*out, self->player_.ptr, 1);
    auto head = [](void* list) { return *static_cast<void**>(list); };
    auto next = [](void* node) { return *static_cast<void**>(node); };
    auto value = [](void* node, std::size_t at) { return *reinterpret_cast<void**>(static_cast<std::uint8_t*>(node) + at); };
    for (void* n = next(head(&self->timers_)); n != head(&self->timers_); n = next(n)) PushReplicated(*out, value(n, 0xC), 1);
    for (void* n = next(head(&self->fileRequests_)); n != head(&self->fileRequests_); n = next(n)) PushReplicated(*out, value(n, 8), 1);
    if (void* group = self->party_.ptr) {
        PushReplicatedInline(*out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            auto* members = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(self->party_.ptr) + 0xC);
            auto* begin = static_cast<void**>(members[0]);
            if (i >= static_cast<std::uint32_t>(static_cast<void**>(members[1]) - begin)) break;
            PushReplicatedInline(*out, begin[i], 1);
        }
    }
}
namespace {
void __fastcall ClientCollectReplicatedHook(ClientSession* self, void*, ReplicatedVector** out) {
    self->ClientSession::CollectReplicated(out);
}
} // namespace
EI_REPLACE(0x0041D050, ClientCollectReplicatedHook);

} // namespace net
