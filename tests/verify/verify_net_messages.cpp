// Checks for net_messages.cpp (the message classes, the server entry, the message base).
//  - Constructor bodies: the original on a scratch copy of the object's bytes, ours on the real
//    object. The ones that allocate string blocks are compared with netverify::BuiltCompare
//    (pointers to the string blocks pair up), then the scratch copy's blocks go back to the pool.
//  - Write / read of the classes without object references: side by side. Write: the original
//    through a copy of the cursor into the cursor's buffer, then ours over it; the bytes and the
//    end position must agree. Read: the original reads into the message, its fields are captured
//    through our write, then ours reads the same bytes again; fields and end position must agree.
//  - Factories, clones, resets, destructor bodies and the classes with object references: ours
//    only (they allocate, release references or record objects in the connection), with the
//    vtable, version and reference count checked.
// The slot wrappers call the qualified members (rule 4). Scenario: any multiplayer session
// (the server and client constructors register every class; a game exchanges them).
#include "net_verify.h"

#include "../../src/game/net_messages.h"
#include "../../src/game/core_refcount.h"
#include "../../src/lib/stlp.h"

#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

using namespace net;
using CtorFn = Message*(__fastcall*)(Message*);
using SlotFn = void(__fastcall*)(void*, void*, BufferCursor*);

void* s_unused;

std::uintptr_t VtableOf(const void* p) {
    std::uintptr_t v;
    std::memcpy(&v, p, 4);
    return v;
}
std::uint32_t Dword(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}

// ---- The message base and the constructor bodies -------------------------------------------------

// 00413250: ours only (it allocates). The count must be cleared.
void* __fastcall NewRefObjectVerify(std::size_t size) {
    void* object = NewRefObject(size);
    ei::verify::Check("net::NewRefObject", true, object != nullptr && static_cast<std::uint32_t*>(object)[1] == 0,
                      "size %u", unsigned(size));
    return object;
}

// Constructors that only store fields.
template <std::size_t N, typename Ours>
Message* CheckPlain(const char* name, CtorFn original, Ours ours, Message* self) {
    alignas(4) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    Message* expectedRet = original(reinterpret_cast<Message*>(scratch));
    Message* actualRet = ours(self);
    bool same = expectedRet == reinterpret_cast<Message*>(scratch) && actualRet == self &&
                std::memcmp(scratch, self, N) == 0;
    ei::verify::Check(name, true, same, "");
    return actualRet;
}

// Constructors that allocate string blocks at `strings` offsets.
template <std::size_t N, typename Ours>
Message* CheckBuilt(const char* name, CtorFn original, Ours ours, Message* self, std::initializer_list<std::size_t> strings) {
    alignas(4) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    Message* expectedRet = original(reinterpret_cast<Message*>(scratch));
    Message* actualRet = ours(self);
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), N};
    cmp.Range(0, N);
    for (std::size_t s : strings) {   // each string: empty, NUL-terminated, 8 bytes of room
        auto* e = reinterpret_cast<StringRep*>(scratch + s);
        auto* a = reinterpret_cast<StringRep*>(reinterpret_cast<std::uint8_t*>(self) + s);
        bool shape = e->start == e->finish && e->eos == e->start + 8 && *e->start == 0 &&
                     a->start == a->finish && a->eos == a->start + 8 && *a->start == 0;
        if (!shape && cmp.ok) std::snprintf(cmp.detail, sizeof(cmp.detail), "string at +0x%X", unsigned(s));
        cmp.ok &= shape;
        stlp::deallocate(e->start, static_cast<std::size_t>(e->eos - e->start));
    }
    ei::verify::Check(name, true, cmp.ok && expectedRet == reinterpret_cast<Message*>(scratch) && actualRet == self,
                      "%s", cmp.detail);
    return actualRet;
}

template <typename T>
Message* ConstructOurs(Message* self) { return static_cast<T*>(self)->Construct(); }

// 004186E0: MOV EAX,ECX (2), MOV [EAX],imm32 (6).
CtorFn s_base;
Message* __fastcall BaseVerify(Message* self) {
    return CheckPlain<12>("net::MessageConstructBase", s_base, MessageConstructBase, self);
}

// 0041C870 / 004140B0: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+8],1 (7). The others: PUSH -1
// (2), PUSH imm32 (5).
CtorFn s_binding, s_request, s_player, s_line, s_host, s_zone, s_login, s_say, s_command;
Message* __fastcall BindingConstructVerify(Message* self) {
    return CheckPlain<0x14>("BindingMessage::Construct", s_binding, ConstructOurs<BindingMessage>, self);
}
Message* __fastcall RequestConstructVerify(Message* self) {
    return CheckPlain<0x14>("FileRequestMessage::Construct", s_request, ConstructOurs<FileRequestMessage>, self);
}
Message* __fastcall PlayerConstructVerify(Message* self) {
    return CheckBuilt<0x3C>("PlayerMessage::Construct", s_player, ConstructOurs<PlayerMessage>, self, {0xC, 0x18});
}
Message* __fastcall LineConstructVerify(Message* self) {
    // +0xC is not written by either.
    return CheckBuilt<0x20>("LineMessage::Construct", s_line, ConstructOurs<LineMessage>, self, {0x10});
}
Message* __fastcall HostConstructVerify(Message* self) {
    return CheckBuilt<0x24>("HostMessage::Construct", s_host, ConstructOurs<HostMessage>, self, {0xC, 0x18});
}
Message* __fastcall ZoneConstructVerify(Message* self) {
    return CheckBuilt<0x18>("ZoneMessage::Construct", s_zone, ConstructOurs<ZoneMessage>, self, {0xC});
}
Message* __fastcall LoginConstructVerify(Message* self) {
    return CheckBuilt<0x34>("LoginMessage::Construct", s_login, ConstructOurs<LoginMessage>, self, {0xC, 0x18});
}
Message* __fastcall SayConstructVerify(Message* self) {
    return CheckBuilt<0x18>("SayMessage::Construct", s_say, ConstructOurs<SayMessage>, self, {0xC});
}
Message* __fastcall CommandConstructVerify(Message* self) {
    return CheckBuilt<0x1C>("CommandMessage::Construct", s_command, ConstructOurs<CommandMessage>, self, {0x10});
}

// ---- The slots --------------------------------------------------------------------------------------

template <typename T>
void OursWrite(void* self, BufferCursor* cursor) { static_cast<T*>(self)->T::Write(cursor); }
template <typename T>
void OursRead(void* self, BufferCursor* cursor) { static_cast<T*>(self)->T::Read(cursor); }

template <typename T>
void CheckWrite(const char* name, SlotFn original, void* self, BufferCursor* cursor) {
    std::uint8_t* start = cursor->p;
    BufferCursor expectedCursor = *cursor;
    original(self, nullptr, &expectedCursor);
    std::vector<std::uint8_t> expected(start, expectedCursor.p);
    OursWrite<T>(self, cursor);
    bool ok = cursor->p == expectedCursor.p && (expected.empty() || std::memcmp(start, expected.data(), expected.size()) == 0);
    ei::verify::Check(name, true, ok, "%d bytes", static_cast<int>(expected.size()));
}

template <typename T>
void CheckRead(const char* name, SlotFn original, void* self, BufferCursor* cursor) {
    BufferCursor expectedCursor = *cursor;
    original(self, nullptr, &expectedCursor);
    std::size_t used = static_cast<std::size_t>(expectedCursor.p - cursor->p);
    std::vector<std::uint8_t> expected(used + 0x100), actual(used + 0x100);
    BufferCursor capture{};
    capture.p = expected.data();
    OursWrite<T>(self, &capture);
    std::size_t expectedSize = static_cast<std::size_t>(capture.p - expected.data());
    OursRead<T>(self, cursor);
    capture.p = actual.data();
    OursWrite<T>(self, &capture);
    std::size_t actualSize = static_cast<std::size_t>(capture.p - actual.data());
    bool ok = cursor->p == expectedCursor.p && expectedSize == actualSize &&
              std::memcmp(expected.data(), actual.data(), expectedSize) == 0;
    ei::verify::Check(name, true, ok, "%d bytes", static_cast<int>(used));
}

void CheckMade(const char* name, const void* made, std::uintptr_t vtable, std::uint32_t version) {
    bool ok = !made || (VtableOf(made) == vtable && Dword(made, 8) == version && (Dword(made, 4) & 0xFFFF) == 0);
    ei::verify::Check(name, true, ok, "");
}

// Factory, clone, reset and destructor body (ours only) of class `Cls`.
#define EI_MESSAGE_LIFETIME_VERIFY(Cls)                                                                    \
    Message* __fastcall Cls##CreateVerify() {                                                              \
        Message* made = Cls::Create();                                                                     \
        CheckMade(#Cls "::Create", made, Cls::kVtableAddress, 1);                                          \
        return made;                                                                                       \
    }                                                                                                      \
    Cls* __fastcall Cls##CloneVerify(Cls* self) {                                                          \
        Cls* made = self->Cls::Clone();                                                                    \
        CheckMade(#Cls "::Clone", made, Cls::kVtableAddress, self->version_);                              \
        return made;                                                                                       \
    }                                                                                                      \
    int __fastcall Cls##ResetVerify(Cls* self) {                                                           \
        int result = self->Cls::OnShutdown();                                                              \
        ei::verify::Check(#Cls "::OnShutdown", true, VtableOf(self) == Cls::kVtableAddress && self->version_ == 1, ""); \
        return result;                                                                                     \
    }                                                                                                      \
    void __fastcall Cls##DestructVerify(Cls* self) {                                                       \
        self->Destruct();                                                                                  \
        ei::verify::Check(#Cls "::Destruct", true, VtableOf(self) == CRefCounted::kVtableAddress, "");            \
    }

// Write and read side by side.
#define EI_MESSAGE_FIELDS_VERIFY(Cls)                                                                      \
    SlotFn s_##Cls##Write, s_##Cls##Read;                                                                  \
    void __fastcall Cls##WriteVerify(void* self, void*, BufferCursor* cursor) {                            \
        CheckWrite<Cls>(#Cls "::Write", s_##Cls##Write, self, cursor);                                     \
    }                                                                                                      \
    void __fastcall Cls##ReadVerify(void* self, void*, BufferCursor* cursor) {                             \
        CheckRead<Cls>(#Cls "::Read", s_##Cls##Read, self, cursor);                                        \
    }

// Write and read with object references: ours only.
#define EI_MESSAGE_OBJECTS_VERIFY(Cls)                                                                     \
    void __fastcall Cls##WriteVerify(Cls* self, void*, BufferCursor* cursor) {                             \
        self->Cls::Write(cursor);                                                                          \
        ei::verify::Check(#Cls "::Write", true, true, "");                                                 \
    }                                                                                                      \
    void __fastcall Cls##ReadVerify(Cls* self, void*, BufferCursor* cursor) {                              \
        self->Cls::Read(cursor);                                                                           \
        ei::verify::Check(#Cls "::Read", true, true, "");                                                  \
    }

EI_MESSAGE_LIFETIME_VERIFY(LoginMessage)
EI_MESSAGE_LIFETIME_VERIFY(SayMessage)
EI_MESSAGE_LIFETIME_VERIFY(CommandMessage)
EI_MESSAGE_LIFETIME_VERIFY(FileRequestMessage)
EI_MESSAGE_LIFETIME_VERIFY(PartyMessage)
EI_MESSAGE_LIFETIME_VERIFY(PartyMemberMessage)
EI_MESSAGE_LIFETIME_VERIFY(PlayerMessage)
EI_MESSAGE_LIFETIME_VERIFY(LineMessage)
EI_MESSAGE_LIFETIME_VERIFY(HostMessage)
EI_MESSAGE_LIFETIME_VERIFY(BindingMessage)
EI_MESSAGE_LIFETIME_VERIFY(ZoneMessage)
EI_MESSAGE_LIFETIME_VERIFY(MapMessage)
EI_MESSAGE_LIFETIME_VERIFY(VariableMessage)
EI_MESSAGE_LIFETIME_VERIFY(FileMessage)
EI_MESSAGE_LIFETIME_VERIFY(FileChunkMessage)
EI_MESSAGE_LIFETIME_VERIFY(AwardMessage)
EI_MESSAGE_LIFETIME_VERIFY(TradeMessage)

EI_MESSAGE_FIELDS_VERIFY(LoginMessage)
EI_MESSAGE_FIELDS_VERIFY(SayMessage)
EI_MESSAGE_FIELDS_VERIFY(CommandMessage)
EI_MESSAGE_FIELDS_VERIFY(PartyMemberMessage)
EI_MESSAGE_FIELDS_VERIFY(PlayerMessage)
EI_MESSAGE_FIELDS_VERIFY(LineMessage)
EI_MESSAGE_FIELDS_VERIFY(HostMessage)
EI_MESSAGE_FIELDS_VERIFY(ZoneMessage)
EI_MESSAGE_FIELDS_VERIFY(MapMessage)
EI_MESSAGE_FIELDS_VERIFY(VariableMessage)
EI_MESSAGE_FIELDS_VERIFY(FileMessage)
EI_MESSAGE_FIELDS_VERIFY(AwardMessage)
EI_MESSAGE_FIELDS_VERIFY(ServerEntry)

EI_MESSAGE_OBJECTS_VERIFY(FileRequestMessage)
EI_MESSAGE_OBJECTS_VERIFY(PartyMessage)
EI_MESSAGE_OBJECTS_VERIFY(BindingMessage)
EI_MESSAGE_OBJECTS_VERIFY(FileChunkMessage)
EI_MESSAGE_OBJECTS_VERIFY(TradeMessage)

// The server entry has no factory; its reset's base constructor clears +8.
ServerEntry* __fastcall ServerEntryCloneVerify(ServerEntry* self) {
    ServerEntry* made = self->ServerEntry::Clone();
    CheckMade("ServerEntry::Clone", made, ServerEntry::kVtableAddress, Dword(self, 8));
    return made;
}
int __fastcall ServerEntryResetVerify(ServerEntry* self) {
    int result = self->ServerEntry::OnShutdown();
    ei::verify::Check("ServerEntry::OnShutdown", true,
                      VtableOf(self) == ServerEntry::kVtableAddress && Dword(self, 8) == 0 && self->kind_ == 4, "");
    return result;
}
void __fastcall ServerEntryDestructVerify(ServerEntry* self) {
    self->Destruct();
    ei::verify::Check("ServerEntry::Destruct", true, VtableOf(self) == CRefCounted::kVtableAddress, "");
}

#undef EI_MESSAGE_LIFETIME_VERIFY
#undef EI_MESSAGE_FIELDS_VERIFY
#undef EI_MESSAGE_OBJECTS_VERIFY

} // namespace

EI_VERIFY(0x00413250, NewRefObjectVerify, 0, s_unused);
EI_VERIFY(0x004186E0, BaseVerify, 8, s_base);
EI_VERIFY(0x0041C870, BindingConstructVerify, 11, s_binding);
EI_VERIFY(0x004140B0, RequestConstructVerify, 11, s_request);
EI_VERIFY(0x0041C680, PlayerConstructVerify, 7, s_player);
EI_VERIFY(0x0041C740, LineConstructVerify, 7, s_line);
EI_VERIFY(0x0041C7C0, HostConstructVerify, 7, s_host);
EI_VERIFY(0x00418C30, ZoneConstructVerify, 7, s_zone);
EI_VERIFY(0x00413350, LoginConstructVerify, 7, s_login);
EI_VERIFY(0x004138A0, SayConstructVerify, 7, s_say);
EI_VERIFY(0x00413C90, CommandConstructVerify, 7, s_command);

// Factory, clone, reset, destructor body (ours only), write, read. Write: PUSH EBX, MOV EBX,ECX,
// PUSH EBP, PUSH ESI (5) and the like; read: PUSH EBX, PUSH ESI, MOV ESI,[ESP+0xC] (6) and the like.
EI_VERIFY(0x00412590, ServerEntryCloneVerify, 0, s_unused);
EI_VERIFY(0x00412750, ServerEntryResetVerify, 0, s_unused);
EI_VERIFY(0x004128E0, ServerEntryDestructVerify, 0, s_unused);
EI_VERIFY(0x00412990, ServerEntryWriteVerify, 5, s_ServerEntryWrite);
EI_VERIFY(0x00412A30, ServerEntryReadVerify, 6, s_ServerEntryRead);
EI_VERIFY(0x00414420, PlayerMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00414500, PlayerMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00414650, PlayerMessageResetVerify, 0, s_unused);
EI_VERIFY(0x004148E0, PlayerMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00414770, PlayerMessageWriteVerify, 5, s_PlayerMessageWrite);
EI_VERIFY(0x00414820, PlayerMessageReadVerify, 6, s_PlayerMessageRead);
EI_VERIFY(0x00414960, LineMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00414A00, LineMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00414AC0, LineMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00414D10, LineMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00414BB0, LineMessageWriteVerify, 5, s_LineMessageWrite);
EI_VERIFY(0x00414C00, LineMessageReadVerify, 7, s_LineMessageRead);
EI_VERIFY(0x00414DB0, HostMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00414E80, HostMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00414FA0, HostMessageResetVerify, 0, s_unused);
EI_VERIFY(0x004152A0, HostMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x004150B0, HostMessageWriteVerify, 5, s_HostMessageWrite);
EI_VERIFY(0x00415120, HostMessageReadVerify, 7, s_HostMessageRead);
EI_VERIFY(0x004171A0, SayMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00413930, SayMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00413A50, SayMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00413C40, SayMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00413B10, SayMessageWriteVerify, 5, s_SayMessageWrite);
EI_VERIFY(0x00413B50, SayMessageReadVerify, 6, s_SayMessageRead);
EI_VERIFY(0x00415570, ZoneMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00415620, ZoneMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00415740, ZoneMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00415930, ZoneMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00415800, ZoneMessageWriteVerify, 5, s_ZoneMessageWrite);
EI_VERIFY(0x00415840, ZoneMessageReadVerify, 6, s_ZoneMessageRead);
EI_VERIFY(0x00415D50, MapMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00415E00, MapMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00415F20, MapMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00416110, MapMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00415FE0, MapMessageWriteVerify, 5, s_MapMessageWrite);
EI_VERIFY(0x00416020, MapMessageReadVerify, 6, s_MapMessageRead);
EI_VERIFY(0x00417250, CommandMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00413D20, CommandMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00413E40, CommandMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00414060, CommandMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00413F00, CommandMessageWriteVerify, 5, s_CommandMessageWrite);
EI_VERIFY(0x00413F50, CommandMessageReadVerify, 7, s_CommandMessageRead);
EI_VERIFY(0x00415980, VariableMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00415A30, VariableMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00415B60, VariableMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00415D00, VariableMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00415C20, VariableMessageWriteVerify, 5, s_VariableMessageWrite);
EI_VERIFY(0x00415C80, VariableMessageReadVerify, 6, s_VariableMessageRead);
EI_VERIFY(0x004170A0, LoginMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00413440, LoginMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00413570, LoginMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00413800, LoginMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x004136B0, LoginMessageWriteVerify, 5, s_LoginMessageWrite);
EI_VERIFY(0x00413750, LoginMessageReadVerify, 6, s_LoginMessageRead);
EI_VERIFY(0x00415320, BindingMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00415350, BindingMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x004153B0, BindingMessageResetVerify, 0, s_unused);
EI_VERIFY(0x004154D0, BindingMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00415450, BindingMessageWriteVerify, 0, s_unused);
EI_VERIFY(0x00415470, BindingMessageReadVerify, 0, s_unused);
EI_VERIFY(0x004179E0, FileRequestMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x004140D0, FileRequestMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00414130, FileRequestMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00414270, FileRequestMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x004141F0, FileRequestMessageWriteVerify, 0, s_unused);
EI_VERIFY(0x00414210, FileRequestMessageReadVerify, 0, s_unused);
EI_VERIFY(0x00416160, FileMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00416220, FileMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00416390, FileMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00416640, FileMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x004164D0, FileMessageWriteVerify, 5, s_FileMessageWrite);
EI_VERIFY(0x00416530, FileMessageReadVerify, 6, s_FileMessageRead);
EI_VERIFY(0x004166F0, FileChunkMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00416720, FileChunkMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00416780, FileChunkMessageResetVerify, 0, s_unused);
EI_VERIFY(0x004168E0, FileChunkMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00416800, FileChunkMessageWriteVerify, 0, s_unused);
EI_VERIFY(0x00416850, FileChunkMessageReadVerify, 0, s_unused);
EI_VERIFY(0x00416940, AwardMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00416A10, AwardMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00416B30, AwardMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00416D70, AwardMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00416C00, AwardMessageWriteVerify, 5, s_AwardMessageWrite);
EI_VERIFY(0x00416C60, AwardMessageReadVerify, 6, s_AwardMessageRead);
EI_VERIFY(0x00416DF0, TradeMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00416E30, TradeMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x00416EB0, TradeMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00416FC0, TradeMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00412C80, TradeMessageWriteVerify, 0, s_unused);
EI_VERIFY(0x00412CE0, TradeMessageReadVerify, 0, s_unused);
EI_VERIFY(0x00417300, PartyMemberMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x00417420, PartyMemberMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x004174F0, PartyMemberMessageResetVerify, 0, s_unused);
EI_VERIFY(0x00417620, PartyMemberMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00417560, PartyMemberMessageWriteVerify, 5, s_PartyMemberMessageWrite);
EI_VERIFY(0x004175B0, PartyMemberMessageReadVerify, 5, s_PartyMemberMessageRead);
EI_VERIFY(0x00417650, PartyMessageCreateVerify, 0, s_unused);
EI_VERIFY(0x004176A0, PartyMessageCloneVerify, 0, s_unused);
EI_VERIFY(0x004177A0, PartyMessageResetVerify, 0, s_unused);
EI_VERIFY(0x004178F0, PartyMessageDestructVerify, 0, s_unused);
EI_VERIFY(0x00412AC0, PartyMessageWriteVerify, 0, s_unused);
EI_VERIFY(0x00412B70, PartyMessageReadVerify, 0, s_unused);
