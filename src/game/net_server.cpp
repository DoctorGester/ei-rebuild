// The game server session (vtable 0073BB70, server base 0073BBB4; pointer at 0079B5DC). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "ei/hook.h"

#include <cstring>

namespace net {

namespace {

// 004409A0: opens the session's UDP socket, bound to `port` when it is positive. Returns 1 / 0.
__declspec(naked) int __fastcall SocketDriverOpen(SocketDriver* /*driver*/, void*, int /*port*/) EI_ORIGINAL(0x004409A0)
// 00421800: STLport string constructor with room for n bytes, library.
__declspec(naked) StringRep* __fastcall StringReserve(StringRep* /*s*/, void*, const void* /*alloc*/, std::size_t /*n*/) EI_ORIGINAL(0x00421800)
// 00418530: fills the server's list at +0x25AC from the game's settings; not rewritten yet.
__declspec(naked) void __fastcall ServerLoadList(ServerSession* /*server*/) EI_ORIGINAL(0x00418530)

// STLport list constructors, list taking the allocator; one per element type, library.
__declspec(naked) void* __fastcall ListConstruct41FD30(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0041FD30)
__declspec(naked) void* __fastcall ListConstruct422400(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422400)
__declspec(naked) void* __fastcall ListConstruct4225E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004225E0)
__declspec(naked) void* __fastcall ListConstruct422800(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422800)
__declspec(naked) void* __fastcall ListConstruct4229E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004229E0)
__declspec(naked) void* __fastcall ListConstruct422BE0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422BE0)
__declspec(naked) void* __fastcall ListConstruct422DC0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422DC0)
__declspec(naked) void* __fastcall ListConstruct4221E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004221E0)
__declspec(naked) void* __fastcall ListConstruct422FE0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422FE0)
__declspec(naked) void* __fastcall ListConstruct4231C0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004231C0)
__declspec(naked) void* __fastcall ListConstruct423440(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423440)
__declspec(naked) void* __fastcall ListConstruct423700(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423700)
__declspec(naked) void* __fastcall ListConstruct421B20(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00421B20)

// Handler registration templates, one per message class; not rewritten yet.
struct ServerRegistrar {
    void Register4279B0(int channel, void* handler);
    void Register427AC0(int channel, void* handler);
    void Register427C40(int channel, void* handler);
};
__declspec(naked) void ServerRegistrar::Register4279B0(int, void*) EI_ORIGINAL(0x004279B0)
__declspec(naked) void ServerRegistrar::Register427AC0(int, void*) EI_ORIGINAL(0x00427AC0)
__declspec(naked) void ServerRegistrar::Register427C40(int, void*) EI_ORIGINAL(0x00427C40)

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

// 00783E30: the game's UDP port (read at run time, as the original does).
int GamePort() { return *reinterpret_cast<const volatile int*>(0x00783E30); }

} // namespace

ServerSession* ServerSession::Construct(std::uint32_t arg) {
    char allocator = 0;   // the containers' (empty) allocator argument
    base.Construct();
    ListConstruct41FD30(&list_2568, nullptr, &allocator);
    SetVtable(this, kServerBaseVtable);
    SocketDriverOpen(&base.socket, nullptr, GamePort());
    unknown_257C[0] = 0;
    unknown_257C[1] = 0;
    unknown_257C[2] = 0;
    unknown_257C[3] = 0;
    ListConstruct422400(&list_2590, nullptr, &allocator);
    ListConstruct4225E0(&list_2594, nullptr, &allocator);
    ListConstruct422800(&list_2598, nullptr, &allocator);
    ListConstruct4229E0(&list_259C, nullptr, &allocator);
    unknown_25A0 = 0;
    ListConstruct422BE0(&list_25A4, nullptr, &allocator);
    unknown_25A8 = 0;
    ListConstruct422DC0(&list_25AC, nullptr, &allocator);
    ListConstruct4221E0(&list_25B0, nullptr, &allocator);
    ListConstruct422FE0(&list_25B4, nullptr, &allocator);
    ListConstruct4231C0(&list_25B8, nullptr, &allocator);
    ListConstruct423440(&list_25BC, nullptr, &allocator);
    ListConstruct423700(&list_25C0, nullptr, &allocator);
    ListConstruct421B20(&list_25C4, nullptr, &allocator);
    StringReserve(&string_25C8, nullptr, &allocator, 8);
    *string_25C8.finish = 0;
    StringReserve(&string_25D4, nullptr, &allocator, 8);
    *string_25D4.finish = 0;
    SetVtable(this, kServerVtable);
    base.unknown_08 = kSessionMagic;
    base.RegisterMessageTypes();

    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 0, Handler(0x0041ADB0));
    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 1, Handler(0x0041AD80));
    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 2, Handler(0x0041B0C0));
    base.AddHandlerFor(0x18, ConstructMessage73BC28, 0, Handler(0x0041B1C0));
    base.AddHandlerFor(0x1C, ConstructMessage73BC40, 0, Handler(0x0041B2A0));
    base.AddHandlerFor(0x14, ConstructMessage73BC58, 0, Handler(0x0041B690));

    auto* registrar = reinterpret_cast<ServerRegistrar*>(this);
    registrar->Register4279B0(2, Handler(0x0041B720));
    registrar->Register427AC0(0, Handler(0x0041B7A0));
    registrar->Register427AC0(1, Handler(0x0041B970));
    registrar->Register427AC0(2, Handler(0x0041BA30));
    registrar->Register427C40(0, Handler(0x0041BA20));
    registrar->Register427C40(1, Handler(0x0041BA20));

    unknown_258C = 0;
    unknown_25E4 = arg;
    unknown_25E0 = 8;
    ServerLoadList(this);
    unknown_2570 = 0;
    unknown_2574 = 0;
    unknown_2578 = 0;
    for (std::uint32_t& v : unknown_25E8) v = 0;
    return this;
}
EI_REPLACE_METHOD(0x00412D80, ServerSession::Construct);

} // namespace net
