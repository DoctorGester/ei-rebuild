// The game client session (vtable 0073BDEC, client base 0073BE30; pointer at 0079B5D8). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// 004DA7F0: the growing byte buffer's constructor (net_coder.h, GameBuffer).
__declspec(naked) void* __fastcall GameBufferConstruct(void* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
// 004409A0: opens the session's UDP socket (bound to `port` when it is positive). Returns 1 / 0.
__declspec(naked) int __fastcall SocketDriverOpen(SocketDriver* /*driver*/, void*, int /*port*/) EI_ORIGINAL(0x004409A0)
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

// Handler registration templates (one per message class): make a prototype, look up its type id
// and add `handler` for it on `channel`. Not rewritten yet.
struct HandlerRegistrar {
    void Register428090(int channel, void* handler);
    void Register4281C0(int channel, void* handler);
    void Register4282F0(int channel, void* handler);
    void Register428440(int channel, void* handler);
    void Register428550(int channel, void* handler);
    void Register428690(int channel, void* handler);
    void Register4287D0(int channel, void* handler);
    void Register4288E0(int channel, void* handler);
};
__declspec(naked) void HandlerRegistrar::Register428090(int, void*) EI_ORIGINAL(0x00428090)
__declspec(naked) void HandlerRegistrar::Register4281C0(int, void*) EI_ORIGINAL(0x004281C0)
__declspec(naked) void HandlerRegistrar::Register4282F0(int, void*) EI_ORIGINAL(0x004282F0)
__declspec(naked) void HandlerRegistrar::Register428440(int, void*) EI_ORIGINAL(0x00428440)
__declspec(naked) void HandlerRegistrar::Register428550(int, void*) EI_ORIGINAL(0x00428550)
__declspec(naked) void HandlerRegistrar::Register428690(int, void*) EI_ORIGINAL(0x00428690)
__declspec(naked) void HandlerRegistrar::Register4287D0(int, void*) EI_ORIGINAL(0x004287D0)
__declspec(naked) void HandlerRegistrar::Register4288E0(int, void*) EI_ORIGINAL(0x004288E0)

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

void RegisterInline(ClientSession* client, std::size_t size, Message* (__fastcall* construct)(Message*),
                    int channel, std::uintptr_t handler) {
    client->base.AddHandlerFor(size, construct, channel, Handler(handler));
}

} // namespace

// 0041C420: the 0x10-byte member at +0x25AC (a vector and a dword), all zero. Returns this.
void* __fastcall ClientVectorConstruct(std::uint32_t* self) {
    self[0] = 0;
    self[1] = 0;
    self[2] = 0;
    self[3] = 0;
    return self;
}
EI_REPLACE(0x0041C420, ClientVectorConstruct);

ClientSession* ClientSession::Construct() {
    char allocator = 0;   // the lists' (empty) allocator argument
    base.Construct();
    unknown_2568[0] = 0;
    unknown_2568[1] = 0;
    unknown_2568[2] = 0;
    unknown_2568[3] = 0;
    GameBufferConstruct(buffer);
    ListConstruct41F8B0(&list_25A8, nullptr, &allocator);
    ClientVectorConstruct(vector_25AC);
    ListConstruct41FB10(&list_25BC, nullptr, &allocator);
    unknown_25C0[0] = 0;
    unknown_25C0[1] = 0;
    unknown_25C0[2] = 0;
    unknown_25C0[3] = 0;
    SetVtable(this, kClientBaseVtable);
    unknown_2598 = 0;
    unknown_2594 = 0;
    SocketDriverOpen(&base.socket, nullptr, -1);
    ListConstruct423990(&list_25D8, nullptr, &allocator);
    ListConstruct423B80(&list_25DC, nullptr, &allocator);
    ListConstruct423D70(&list_25E0, nullptr, &allocator);
    ListConstruct423F50(&list_25E4, nullptr, &allocator);
    ListConstruct424130(&list_25E8, nullptr, &allocator);
    ref_25EC = nullptr;
    ref_25F0 = nullptr;
    ref_25F4 = nullptr;
    ListConstruct424320(&list_25F8, nullptr, &allocator);
    ListConstruct424510(&list_25FC, nullptr, &allocator);
    player = nullptr;
    ListConstruct420F10(&list_2604, nullptr, &allocator);
    ListConstruct424750(&list_2608, nullptr, &allocator);
    unknown_260C = nullptr;
    ListConstruct4231C0(&list_2610, nullptr, &allocator);
    ListConstruct4249E0(&list_2614, nullptr, &allocator);
    unknown_2618 = nullptr;
    UnitRecordBuilderConstruct(&records[0]);
    UnitRecordBuilderConstruct(&records[1]);
    SetVtable(this, kClientVtable);

    // The local player's record (vtable 0073BBF8): two strings, +0x24 = 0, +0x28 = 4, +0x2C = -1.
    auto* record = static_cast<Message*>(gamecrt::operator_new(0x34));
    reinterpret_cast<std::uint32_t*>(record)[1] = 0;   // before the null check, as in the original
    if (record) {
        MessageConstructBase(record);
        auto* bytes = reinterpret_cast<std::uint8_t*>(record);
        StringConstruct(reinterpret_cast<StringRep*>(bytes + 0xC));
        StringConstruct(reinterpret_cast<StringRep*>(bytes + 0x18));
        *reinterpret_cast<std::uint32_t*>(bytes + 0x30) = 0;   // 0041FFE0
        SetVtable(record, 0x0073BBF8);
        bytes[0x24] = 0;
        *reinterpret_cast<std::uint32_t*>(bytes + 0x28) = 4;
        *reinterpret_cast<std::uint32_t*>(bytes + 0x2C) = 0xFFFFFFFFu;
    }
    auto* playerRef = reinterpret_cast<RefPtr*>(&player);
    if (reinterpret_cast<void*>(record) != player) {
        playerRef->Destroy();
        playerRef->Share(reinterpret_cast<CRefObject*>(record));
    }

    base.unknown_08 = kSessionMagic;
    base.RegisterMessageTypes();

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

    auto* registrar = reinterpret_cast<HandlerRegistrar*>(this);
    registrar->Register428090(0, Handler(0x0041E900));
    registrar->Register428090(2, Handler(0x0041E960));
    registrar->Register4281C0(0, Handler(0x0041E990));
    registrar->Register4281C0(1, Handler(0x0041EA10));
    registrar->Register4281C0(2, Handler(0x0041EA20));
    registrar->Register4282F0(0, Handler(0x0041EAC0));
    registrar->Register4282F0(2, Handler(0x0041EBF0));
    registrar->Register428440(0, Handler(0x0041ECC0));   // patched by Rebirth: passed by address
    registrar->Register428440(2, Handler(0x0041EF10));
    registrar->Register428550(0, Handler(0x0041EF90));
    registrar->Register428690(0, Handler(0x0041EFF0));
    registrar->Register428690(1, Handler(0x0041F090));
    registrar->Register428690(2, Handler(0x0041F0D0));
    registrar->Register4287D0(0, Handler(0x0041F150));
    registrar->Register4287D0(1, Handler(0x0041F180));
    registrar->Register4287D0(2, Handler(0x0041F1B0));
    registrar->Register4288E0(0, Handler(0x0041F1E0));
    registrar->Register4288E0(1, Handler(0x0041F270));
    registrar->Register4288E0(2, Handler(0x0041F3A0));

    ei::VCall<void>(&records[0], 2);
    ei::VCall<void>(&records[1], 2);
    return this;
}
EI_REPLACE_METHOD(0x0041BC40, ClientSession::Construct);

} // namespace net
