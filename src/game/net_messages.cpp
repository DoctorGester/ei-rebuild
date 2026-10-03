// Network message objects (vtable base 0073BC10) and their helpers. See net_classes.h and
// docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>

namespace net {

namespace {

// 00424E40: STLport _String_base::_M_allocate_block(n) (pool block of n bytes; start = finish).
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

StringRep* StringAt(Message* m, std::size_t offset) {
    return reinterpret_cast<StringRep*>(reinterpret_cast<std::uint8_t*>(m) + offset);
}

// An empty string with an 8-byte block from _M_allocate_block (the constructors that call 00424E40).
void InitStringByBlock(StringRep* s) {
    s->start = nullptr;
    s->finish = nullptr;
    s->eos = nullptr;
    StringAllocateBlock(s, nullptr, 8);
    *s->finish = 0;
}

// The same with the node allocator called directly (the constructors that inline it).
void InitStringInline(StringRep* s) {
    s->start = nullptr;
    s->finish = nullptr;
    s->eos = nullptr;
    char* block = static_cast<char*>(stlp::allocate(8));
    s->start = block;
    s->finish = block;
    s->eos = block + 8;
    *block = 0;
}

std::uint32_t& Field(Message* m, std::size_t offset) {
    return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(m) + offset);
}
std::uint8_t& Byte(Message* m, std::size_t offset) { return reinterpret_cast<std::uint8_t*>(m)[offset]; }

} // namespace

void* __fastcall NewRefObject(std::size_t size) {
    void* object = gamecrt::operator_new(size);
    static_cast<std::uint32_t*>(object)[1] = 0;
    return object;
}
EI_REPLACE(0x00413250, NewRefObject);

Message* __fastcall MessageConstructBase(Message* self) {
    SetVtable(self, kMessageVtable);
    self->kind = 1;
    return self;
}
EI_REPLACE(0x004186E0, MessageConstructBase);

// 0041C870 (0x14 bytes): two dwords, both 0.
Message* __fastcall ConstructMessage73BCB8(Message* self) {
    self->kind = 1;
    Field(self, 0xC) = 0;
    Field(self, 0x10) = 0;
    SetVtable(self, 0x0073BCB8);
    return self;
}
EI_REPLACE(0x0041C870, ConstructMessage73BCB8);

// 0041C680 (0x3C bytes): two strings, +0x24 = 1, +0x28 = 0, +0x2C / +0x30 / +0x34 = -1.
Message* __fastcall ConstructMessage73BC70(Message* self) {
    MessageConstructBase(self);
    InitStringByBlock(StringAt(self, 0xC));
    InitStringInline(StringAt(self, 0x18));
    SetVtable(self, 0x0073BC70);
    Field(self, 0x2C) = 0xFFFFFFFFu;
    Field(self, 0x34) = 0xFFFFFFFFu;
    Field(self, 0x30) = 0xFFFFFFFFu;
    Byte(self, 0x28) = 0;
    Field(self, 0x24) = 1;
    return self;
}
EI_REPLACE(0x0041C680, ConstructMessage73BC70);

// 0041C740 (0x20 bytes): a string at +0x10, +0x1C = 0 (+0xC is not set).
Message* __fastcall ConstructMessage73BC88(Message* self) {
    MessageConstructBase(self);
    InitStringByBlock(StringAt(self, 0x10));
    Field(self, 0x1C) = 0;
    SetVtable(self, 0x0073BC88);
    return self;
}
EI_REPLACE(0x0041C740, ConstructMessage73BC88);

// 0041C7C0 (0x24 bytes): two strings.
Message* __fastcall ConstructMessage73BCA0(Message* self) {
    MessageConstructBase(self);
    InitStringByBlock(StringAt(self, 0xC));
    InitStringInline(StringAt(self, 0x18));
    SetVtable(self, 0x0073BCA0);
    return self;
}
EI_REPLACE(0x0041C7C0, ConstructMessage73BCA0);

// 00418C30 (0x18 bytes): one string.
Message* __fastcall ConstructMessage73BCD0(Message* self) {
    MessageConstructBase(self);
    InitStringInline(StringAt(self, 0xC));
    SetVtable(self, 0x0073BCD0);
    return self;
}
EI_REPLACE(0x00418C30, ConstructMessage73BCD0);

} // namespace net
