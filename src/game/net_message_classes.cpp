// The network message classes 0073BBF8 .. 0073BDD0 (net_classes.h) and the server entry 0073BB28
// (no factory): for each one its factory (the type registry's, 004142F0), clone (slot 3), reset
// (slot 2: destroyed and built again in place), write / read (slots 4 / 5: the fields at a byte
// cursor) and destructor body. The scalar deleting
// destructors are in deleting_destructors_400.cpp, the constructors used by the sessions in
// net_messages.cpp. See docs/notes/unmapped_410_470.md ("Eighth round").
//
// The original inlines several STLport copies of the same string and vector code (copy
// construction from [first, last) into a fresh block of length + 1 bytes, the 8-byte empty string,
// assignment from a C string); they are reproduced here through one helper each, with the same
// allocations in the same order.
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <initializer_list>

namespace net {

namespace {

// --- STLport (library) ---------------------------------------------------------------------------
__declspec(naked) void* __fastcall NodeAllocate(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
__declspec(naked) void __fastcall NodeFree(void* /*p*/, std::size_t /*bytes*/) EI_ORIGINAL(0x00404D70)
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)
__declspec(naked) void __fastcall StringRangeInit(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00428F10)
__declspec(naked) void __fastcall StringAssignRange(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00426A80)
// vector<RefPtr> of the file message: copy of the elements sharing them, deallocate.
__declspec(naked) RefPtr* __fastcall SharedRefsCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/) EI_ORIGINAL(0x0042AE90)
__declspec(naked) void __fastcall FileRefsDeallocate(void* /*eos proxy*/, void*, RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x004251B0)
// vector<RefPtr> of the 0073BDD0 message: base constructor, copy, deallocate, resize.
__declspec(naked) void* __fastcall RefsGetAllocator(const void* /*vector*/, void*, void* /*out*/) EI_ORIGINAL(0x00421EB0)
__declspec(naked) void __fastcall RefsBaseConstruct(void* /*vector*/, void*, std::size_t /*n*/, const void* /*alloc*/) EI_ORIGINAL(0x00421F60)
__declspec(naked) RefPtr* __fastcall RefsCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/) EI_ORIGINAL(0x0042AF80)
__declspec(naked) void __fastcall RefsDeallocate(void* /*eos proxy*/, void*, RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00425210)
__declspec(naked) void __fastcall RefsResize(void* /*vector*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x004200C0)
// Counted pointer element destructors (deleting destructors called with 0).
__declspec(naked) void* __fastcall FileRefDestroy(RefPtr* /*p*/, void*, unsigned /*flags*/) EI_ORIGINAL(0x0042B880)
__declspec(naked) void* __fastcall RefRelease(RefPtr* /*p*/, void*, unsigned /*flags*/) EI_ORIGINAL(0x004292A0)
// The memory stream of the party member message: reserve / resize (CMemoryStream, 004A0000).
__declspec(naked) void __fastcall StreamReserve(void* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A5570)
__declspec(naked) void __fastcall StreamResize(void* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A54D0)
// The connection being written or read (0079B924): object references (net_connection.cpp).
__declspec(naked) void __fastcall ConnWriteObjectRef(void* /*connection*/, void*, char** /*cursor*/, void* /*object*/) EI_ORIGINAL(0x004355B0)
__declspec(naked) void* __fastcall ConnReadObjectRef(void* /*connection*/, void*, char** /*cursor*/) EI_ORIGINAL(0x00435820)

constexpr std::uintptr_t kRefValueVtable = 0x0073BB58;
constexpr std::uintptr_t kRootVtable = 0x0073BB68;
constexpr std::uintptr_t kMemoryStreamVtable = 0x0073BD90;
constexpr std::uintptr_t kStreamVtable = 0x0073BDB0;

template <typename T>
T& F(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
StringRep* S(void* object, std::size_t offset) { return &F<StringRep>(object, offset); }
void SetVtable(void* object, std::uintptr_t v) { F<std::uintptr_t>(object, 0) = v; }
void* Connection() { return *reinterpret_cast<void**>(0x0079B924); }

// A message's memory: the reference count cleared before the null check (as the original).
std::uint8_t* NewMessage(std::size_t size) {
    auto* p = static_cast<std::uint8_t*>(gamecrt::operator_new(size));
    F<std::uint32_t>(p, 4) = 0;
    return p;
}
// The header of a clone (base constructors inlined) and of a rebuilt message.
void CloneHeader(void* copy, void* source) {
    SetVtable(copy, kRefValueVtable);
    F<std::uint32_t>(copy, 8) = F<std::uint32_t>(source, 8);
    SetVtable(copy, kMessageVtable);
}
void FreshHeader(void* self) {
    SetVtable(self, kMessageVtable);
    F<std::uint32_t>(self, 8) = 1;
}

// Counted pointers as the classes inline them.
void CopyCounted(void* to, std::size_t offset, void* from) {
    auto* object = F<CRefObject*>(from, offset);
    F<CRefObject*>(to, offset) = object;
    if (object) ++object->refs_;
}
void ReleaseCounted(void* self, std::size_t offset) {
    CRefObject* object = F<CRefObject*>(self, offset);
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) ei::VCall<void>(object, 0, 1);
}
void AssignCounted(void* self, std::size_t offset, CRefObject* object) {
    CRefObject* previous = F<CRefObject*>(self, offset);
    F<CRefObject*>(self, offset) = object;
    if (object) ++object->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) ei::VCall<void>(previous, 0, 1);
    }
}

// Strings.
void StringCopy(StringRep* to, const StringRep* from) {
    to->start = nullptr;
    to->finish = nullptr;
    to->eos = nullptr;
    StringRangeInit(to, nullptr, from->start, from->finish);
}
void StringEmpty(StringRep* s) {
    s->start = nullptr;
    s->finish = nullptr;
    s->eos = nullptr;
    StringAllocateBlock(s, nullptr, 8);
    *s->finish = 0;
}
void StringFree(StringRep* s) {
    if (s->start) NodeFree(s->start, static_cast<std::size_t>(s->eos - s->start));
}

// The byte cursor.
template <typename T>
void Put(char** cursor, T value) {
    std::memcpy(*cursor, &value, sizeof(T));
    *cursor += sizeof(T);
}
template <typename T>
T Take(char** cursor) {
    T value;
    std::memcpy(&value, *cursor, sizeof(T));
    *cursor += sizeof(T);
    return value;
}
void PutString(char** cursor, const StringRep* s) {
    std::size_t length = std::strlen(s->start) + 1;
    std::memcpy(*cursor, s->start, length);
    *cursor += length;
}
void TakeString(char** cursor, StringRep* s) {
    const char* text = *cursor;
    std::size_t length = std::strlen(text);
    StringAssignRange(s, nullptr, text, text + length);
    *cursor += length + 1;
}
void PutObject(char** cursor, void* object) { ConnWriteObjectRef(Connection(), nullptr, cursor, object); }
void TakeObject(char** cursor, void* self, std::size_t offset) {
    AssignCounted(self, offset, static_cast<CRefObject*>(ConnReadObjectRef(Connection(), nullptr, cursor)));
}

} // namespace

// ================================================================================================
// 0073BC70 (0x3C, type 1): a player record: name +0xC, text +0x18, +0x24, host +0x28 (byte), +0x2C,
// +0x30, number +0x34, +0x38.
namespace player {
constexpr std::uintptr_t kVtable = 0x0073BC70;
void Init(std::uint8_t* p) {
    StringEmpty(S(p, 0xC));
    StringEmpty(S(p, 0x18));
    F<std::uint32_t>(p, 0x2C) = 0xFFFFFFFFu;
    F<std::uint32_t>(p, 0x34) = 0xFFFFFFFFu;
    F<std::uint32_t>(p, 0x30) = 0xFFFFFFFFu;
    SetVtable(p, kVtable);
    p[0x28] = 0;
    F<std::uint32_t>(p, 0x24) = 1;
}
} // namespace player

Message* __fastcall MessageFactory73BC70() {   // 00414420
    std::uint8_t* p = NewMessage(0x3C);
    if (!p) return nullptr;
    FreshHeader(p);
    player::Init(p);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BC70(Message* self) {   // 00414500
    std::uint8_t* p = NewMessage(0x3C);
    if (!p) return nullptr;
    CloneHeader(p, self);
    StringCopy(S(p, 0xC), S(self, 0xC));
    StringCopy(S(p, 0x18), S(self, 0x18));
    F<std::uint32_t>(p, 0x24) = F<std::uint32_t>(self, 0x24);
    p[0x28] = F<std::uint8_t>(self, 0x28);
    for (std::size_t offset : {0x2Cu, 0x30u, 0x34u, 0x38u}) F<std::uint32_t>(p, offset) = F<std::uint32_t>(self, offset);
    SetVtable(p, player::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BC70(Message* self) {   // 00414650
    SetVtable(self, player::kVtable);
    StringFree(S(self, 0x18));
    StringFree(S(self, 0xC));
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    player::Init(reinterpret_cast<std::uint8_t*>(self));
}
void __fastcall MessageWrite73BC70(Message* self, void*, char** cursor) {   // 00414770
    PutString(cursor, S(self, 0xC));
    PutString(cursor, S(self, 0x18));
    Put(cursor, F<std::uint32_t>(self, 0x24));
    Put(cursor, F<std::uint8_t>(self, 0x28));
    Put(cursor, F<std::uint32_t>(self, 0x2C));
    Put(cursor, F<std::uint32_t>(self, 0x30));
}
void __fastcall MessageRead73BC70(Message* self, void*, char** cursor) {   // 00414820
    TakeString(cursor, S(self, 0xC));
    TakeString(cursor, S(self, 0x18));
    F<std::uint32_t>(self, 0x24) = Take<std::uint32_t>(cursor);
    F<std::uint8_t>(self, 0x28) = Take<std::uint8_t>(cursor);
    F<std::uint32_t>(self, 0x2C) = Take<std::uint32_t>(cursor);
    F<std::uint32_t>(self, 0x30) = Take<std::uint32_t>(cursor);
}
void __fastcall MessageDestruct73BC70(Message* self) {   // 004148E0
    SetVtable(self, player::kVtable);
    StringFree(S(self, 0x18));
    StringFree(S(self, 0xC));
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BC88 (0x20, type 2): a chat line: number +0xC, text +0x10, connection +0x1C (not sent).
namespace line {
constexpr std::uintptr_t kVtable = 0x0073BC88;
}
Message* __fastcall MessageFactory73BC88() {   // 00414960
    std::uint8_t* p = NewMessage(0x20);
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(S(p, 0x10));
    F<std::uint32_t>(p, 0x1C) = 0;
    SetVtable(p, line::kVtable);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BC88(Message* self) {   // 00414A00
    std::uint8_t* p = NewMessage(0x20);
    if (!p) return nullptr;
    CloneHeader(p, self);
    F<std::uint32_t>(p, 0xC) = F<std::uint32_t>(self, 0xC);
    StringCopy(S(p, 0x10), S(self, 0x10));
    CopyCounted(p, 0x1C, self);
    SetVtable(p, line::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BC88(Message* self) {   // 00414AC0
    SetVtable(self, line::kVtable);
    ReleaseCounted(self, 0x1C);
    StringFree(S(self, 0x10));
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    StringEmpty(S(self, 0x10));
    F<std::uint32_t>(self, 0x1C) = 0;
    SetVtable(self, line::kVtable);
}
void __fastcall MessageWrite73BC88(Message* self, void*, char** cursor) {   // 00414BB0
    Put(cursor, F<std::uint32_t>(self, 0xC));
    PutString(cursor, S(self, 0x10));
}
void __fastcall MessageRead73BC88(Message* self, void*, char** cursor) {   // 00414C00
    F<std::uint32_t>(self, 0xC) = Take<std::uint32_t>(cursor);
    TakeString(cursor, S(self, 0x10));
}
void __fastcall MessageDestruct73BC88(Message* self) {   // 00414D10
    SetVtable(self, line::kVtable);
    ReleaseCounted(self, 0x1C);
    StringFree(S(self, 0x10));
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// Messages of one or two strings (and a few dwords): 0073BCA0 (0x24, type 3: +0xC, +0x18),
// 0073BC28 (0x18, type 6: +0xC), 0073BCD0 (0x18, type 8: +0xC), 0073BD00 (0x18, type 9: +0xC),
// 0073BC40 (0x1C, type 7: kind +0xC, text +0x10), 0073BCE8 (0x20, type 10: name +0xC, value +0x18,
// serial +0x1C).
namespace text {
// The string offsets and the dwords around them of each class.
struct Layout {
    std::uintptr_t vtable;
    std::size_t size;
    std::size_t strings[2];   // 0 ends the list
    std::size_t dwordsBefore; // offset of one dword written before the strings (0: none)
    std::size_t dwordsAfter;  // offset of two dwords written after (0: none)
};
constexpr Layout k73BCA0{0x0073BCA0, 0x24, {0xC, 0x18}, 0, 0};
constexpr Layout k73BC28{0x0073BC28, 0x18, {0xC, 0}, 0, 0};
constexpr Layout k73BCD0{0x0073BCD0, 0x18, {0xC, 0}, 0, 0};
constexpr Layout k73BD00{0x0073BD00, 0x18, {0xC, 0}, 0, 0};
constexpr Layout k73BC40{0x0073BC40, 0x1C, {0x10, 0}, 0xC, 0};
constexpr Layout k73BCE8{0x0073BCE8, 0x20, {0xC, 0}, 0, 0x18};

Message* Factory(const Layout& l) {
    std::uint8_t* p = NewMessage(l.size);
    if (!p) return nullptr;
    FreshHeader(p);
    for (std::size_t s : l.strings)
        if (s) StringEmpty(S(p, s));
    SetVtable(p, l.vtable);
    return reinterpret_cast<Message*>(p);
}
Message* Clone(const Layout& l, Message* self) {
    std::uint8_t* p = NewMessage(l.size);
    if (!p) return nullptr;
    CloneHeader(p, self);
    if (l.dwordsBefore) F<std::uint32_t>(p, l.dwordsBefore) = F<std::uint32_t>(self, l.dwordsBefore);
    for (std::size_t s : l.strings)
        if (s) StringCopy(S(p, s), S(self, s));
    if (l.dwordsAfter) {
        F<std::uint32_t>(p, l.dwordsAfter) = F<std::uint32_t>(self, l.dwordsAfter);
        F<std::uint32_t>(p, l.dwordsAfter + 4) = F<std::uint32_t>(self, l.dwordsAfter + 4);
    }
    SetVtable(p, l.vtable);
    return reinterpret_cast<Message*>(p);
}
void FreeStrings(const Layout& l, Message* self) {
    if (l.strings[1]) StringFree(S(self, l.strings[1]));
    StringFree(S(self, l.strings[0]));
}
void Reset(const Layout& l, Message* self) {
    SetVtable(self, l.vtable);
    FreeStrings(l, self);
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    for (std::size_t s : l.strings)
        if (s) StringEmpty(S(self, s));
    SetVtable(self, l.vtable);
}
void Write(const Layout& l, Message* self, char** cursor) {
    if (l.dwordsBefore) Put(cursor, F<std::uint32_t>(self, l.dwordsBefore));
    for (std::size_t s : l.strings)
        if (s) PutString(cursor, S(self, s));
    if (l.dwordsAfter) {
        Put(cursor, F<std::uint32_t>(self, l.dwordsAfter));
        Put(cursor, F<std::uint32_t>(self, l.dwordsAfter + 4));
    }
}
void Read(const Layout& l, Message* self, char** cursor) {
    if (l.dwordsBefore) F<std::uint32_t>(self, l.dwordsBefore) = Take<std::uint32_t>(cursor);
    for (std::size_t s : l.strings)
        if (s) TakeString(cursor, S(self, s));
    if (l.dwordsAfter) {
        F<std::uint32_t>(self, l.dwordsAfter) = Take<std::uint32_t>(cursor);
        F<std::uint32_t>(self, l.dwordsAfter + 4) = Take<std::uint32_t>(cursor);
    }
}
void Destruct(const Layout& l, Message* self) {
    SetVtable(self, l.vtable);
    FreeStrings(l, self);
    SetVtable(self, kRootVtable);
}
} // namespace text

#define EI_TEXT_MESSAGE(cls)                                                                            \
    Message* __fastcall MessageFactory##cls() { return text::Factory(text::k##cls); }                   \
    Message* __fastcall MessageClone##cls(Message* self) { return text::Clone(text::k##cls, self); }    \
    void __fastcall MessageReset##cls(Message* self) { text::Reset(text::k##cls, self); }               \
    void __fastcall MessageWrite##cls(Message* self, void*, char** cursor) {                            \
        text::Write(text::k##cls, self, cursor);                                                        \
    }                                                                                                   \
    void __fastcall MessageRead##cls(Message* self, void*, char** cursor) { text::Read(text::k##cls, self, cursor); } \
    void __fastcall MessageDestruct##cls(Message* self) { text::Destruct(text::k##cls, self); }

EI_TEXT_MESSAGE(73BCA0)
EI_TEXT_MESSAGE(73BC28)
EI_TEXT_MESSAGE(73BCD0)
EI_TEXT_MESSAGE(73BD00)
EI_TEXT_MESSAGE(73BC40)
EI_TEXT_MESSAGE(73BCE8)

// ================================================================================================
// 0073BBF8 (0x34, type 5): the local player record: name +0xC, text +0x18, +0x24 (byte), +0x28,
// +0x2C, counted +0x30 (not sent).
namespace local {
constexpr std::uintptr_t kVtable = 0x0073BBF8;
void Init(std::uint8_t* p) {
    StringEmpty(S(p, 0xC));
    StringEmpty(S(p, 0x18));
    F<std::uint32_t>(p, 0x30) = 0;
    SetVtable(p, kVtable);
    p[0x24] = 0;
    F<std::uint32_t>(p, 0x28) = 4;
    F<std::uint32_t>(p, 0x2C) = 0xFFFFFFFFu;
}
} // namespace local

Message* __fastcall MessageFactory73BBF8() {   // 004170A0
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    FreshHeader(p);
    local::Init(p);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BBF8(Message* self) {   // 00413440
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    CloneHeader(p, self);
    StringCopy(S(p, 0xC), S(self, 0xC));
    StringCopy(S(p, 0x18), S(self, 0x18));
    p[0x24] = F<std::uint8_t>(self, 0x24);
    F<std::uint32_t>(p, 0x28) = F<std::uint32_t>(self, 0x28);
    F<std::uint32_t>(p, 0x2C) = F<std::uint32_t>(self, 0x2C);
    CopyCounted(p, 0x30, self);
    SetVtable(p, local::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BBF8(Message* self) {   // 00413570
    SetVtable(self, local::kVtable);
    ReleaseCounted(self, 0x30);
    StringFree(S(self, 0x18));
    StringFree(S(self, 0xC));
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    local::Init(reinterpret_cast<std::uint8_t*>(self));
}
void __fastcall MessageWrite73BBF8(Message* self, void*, char** cursor) {   // 004136B0
    PutString(cursor, S(self, 0xC));
    PutString(cursor, S(self, 0x18));
    Put(cursor, F<std::uint32_t>(self, 0x28));
    Put(cursor, F<std::uint8_t>(self, 0x24));
    Put(cursor, F<std::uint32_t>(self, 0x2C));
}
void __fastcall MessageRead73BBF8(Message* self, void*, char** cursor) {   // 00413750
    TakeString(cursor, S(self, 0xC));
    TakeString(cursor, S(self, 0x18));
    F<std::uint32_t>(self, 0x28) = Take<std::uint32_t>(cursor);
    F<std::uint8_t>(self, 0x24) = Take<std::uint8_t>(cursor);
    F<std::uint32_t>(self, 0x2C) = Take<std::uint32_t>(cursor);
}
void __fastcall MessageDestruct73BBF8(Message* self) {   // 00413800
    SetVtable(self, local::kVtable);
    ReleaseCounted(self, 0x30);
    StringFree(S(self, 0x18));
    StringFree(S(self, 0xC));
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// Two counted pointers: 0073BCB8 (0x14, type 4: a binding, player +0xC, connection +0x10) and
// 0073BC58 (0x14, type 13: object +0xC, connection +0x10). Only +0xC is sent.
namespace pair {
Message* Factory(std::uintptr_t vtable) {
    std::uint8_t* p = NewMessage(0x14);
    if (!p) return nullptr;
    FreshHeader(p);
    F<std::uint32_t>(p, 0xC) = 0;
    F<std::uint32_t>(p, 0x10) = 0;
    SetVtable(p, vtable);
    return reinterpret_cast<Message*>(p);
}
Message* Clone(std::uintptr_t vtable, Message* self) {
    std::uint8_t* p = NewMessage(0x14);
    if (!p) return nullptr;
    CloneHeader(p, self);
    CopyCounted(p, 0xC, self);
    CopyCounted(p, 0x10, self);
    SetVtable(p, vtable);
    return reinterpret_cast<Message*>(p);
}
void Reset(std::uintptr_t vtable, Message* self) {
    SetVtable(self, vtable);
    ReleaseCounted(self, 0x10);
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
    if (!self) return;
    F<std::uint32_t>(self, 8) = 1;
    F<std::uint32_t>(self, 0xC) = 0;
    F<std::uint32_t>(self, 0x10) = 0;
    SetVtable(self, vtable);
}
void Destruct(std::uintptr_t vtable, Message* self) {
    SetVtable(self, vtable);
    ReleaseCounted(self, 0x10);
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
}
} // namespace pair

#define EI_PAIR_MESSAGE(cls)                                                                            \
    Message* __fastcall MessageFactory##cls() { return pair::Factory(0x00##cls); }                      \
    Message* __fastcall MessageClone##cls(Message* self) { return pair::Clone(0x00##cls, self); }       \
    void __fastcall MessageReset##cls(Message* self) { pair::Reset(0x00##cls, self); }                  \
    void __fastcall MessageWrite##cls(Message* self, void*, char** cursor) { PutObject(cursor, F<void*>(self, 0xC)); } \
    void __fastcall MessageRead##cls(Message* self, void*, char** cursor) { TakeObject(cursor, self, 0xC); } \
    void __fastcall MessageDestruct##cls(Message* self) { pair::Destruct(0x00##cls, self); }

EI_PAIR_MESSAGE(73BCB8)
EI_PAIR_MESSAGE(73BC58)

// ================================================================================================
// 0073BD18 (0x2C, type 11): a shared file: chunk count +0xC (word), size +0x10, name +0x14,
// the chunk messages +0x20 (vector of counted pointers, not sent).
namespace file {
constexpr std::uintptr_t kVtable = 0x0073BD18;
struct Refs {
    RefPtr* start;
    RefPtr* finish;
    RefPtr* eos;
};
Refs* Chunks(void* self) { return &F<Refs>(self, 0x20); }
} // namespace file

Message* __fastcall MessageFactory73BD18() {   // 00416160
    std::uint8_t* p = NewMessage(0x2C);
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(S(p, 0x14));
    file::Refs* chunks = file::Chunks(p);
    chunks->start = nullptr;
    chunks->finish = nullptr;
    chunks->eos = nullptr;
    SetVtable(p, file::kVtable);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BD18(Message* self) {   // 00416220
    std::uint8_t* p = NewMessage(0x2C);
    if (!p) return nullptr;
    CloneHeader(p, self);
    F<std::uint16_t>(p, 0xC) = F<std::uint16_t>(self, 0xC);
    F<std::uint32_t>(p, 0x10) = F<std::uint32_t>(self, 0x10);
    StringCopy(S(p, 0x14), S(self, 0x14));
    file::Refs* from = file::Chunks(self);
    file::Refs* to = file::Chunks(p);
    std::size_t n = static_cast<std::size_t>(from->finish - from->start);
    to->start = nullptr;
    to->finish = nullptr;
    to->eos = nullptr;
    auto* block = static_cast<RefPtr*>(n ? NodeAllocate(n * 4) : nullptr);
    to->start = block;
    to->finish = block;
    to->eos = block + n;
    to->finish = SharedRefsCopy(from->start, from->finish, block, 0);
    SetVtable(p, file::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BD18(Message* self) {   // 00416390
    SetVtable(self, file::kVtable);
    file::Refs* chunks = file::Chunks(self);
    for (RefPtr *p = chunks->start, *end = chunks->finish; p != end; ++p) FileRefDestroy(p, nullptr, 0);
    FileRefsDeallocate(&chunks->eos, nullptr, chunks->start, static_cast<std::size_t>(chunks->eos - chunks->start));
    StringFree(S(self, 0x14));
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    StringEmpty(S(self, 0x14));
    chunks->start = nullptr;
    chunks->finish = nullptr;
    chunks->eos = nullptr;
    SetVtable(self, file::kVtable);
}
void __fastcall MessageWrite73BD18(Message* self, void*, char** cursor) {   // 004164D0
    Put(cursor, F<std::uint16_t>(self, 0xC));
    Put(cursor, F<std::uint32_t>(self, 0x10));
    PutString(cursor, S(self, 0x14));
}
void __fastcall MessageRead73BD18(Message* self, void*, char** cursor) {   // 00416530
    F<std::uint16_t>(self, 0xC) = Take<std::uint16_t>(cursor);
    F<std::uint32_t>(self, 0x10) = Take<std::uint32_t>(cursor);
    TakeString(cursor, S(self, 0x14));
}
void __fastcall MessageDestruct73BD18(Message* self) {   // 00416640
    SetVtable(self, file::kVtable);
    file::Refs* chunks = file::Chunks(self);
    for (RefPtr *p = chunks->start, *end = chunks->finish; p != end; ++p) p->Destroy();
    if (chunks->start) NodeFree(chunks->start, static_cast<std::size_t>(chunks->eos - chunks->start) * 4);
    StringFree(S(self, 0x14));
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BD30 (0x54, type 12): a file chunk: file +0xC (counted), index +0x10 (word), 64 bytes +0x12.
namespace chunk {
constexpr std::uintptr_t kVtable = 0x0073BD30;
}
Message* __fastcall MessageFactory73BD30() {   // 004166F0
    std::uint8_t* p = NewMessage(0x54);
    if (!p) return nullptr;
    F<std::uint32_t>(p, 8) = 1;
    F<std::uint32_t>(p, 0xC) = 0;
    SetVtable(p, chunk::kVtable);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BD30(Message* self) {   // 00416720
    std::uint8_t* p = NewMessage(0x54);
    if (!p) return nullptr;
    CloneHeader(p, self);
    CopyCounted(p, 0xC, self);
    F<std::uint16_t>(p, 0x10) = F<std::uint16_t>(self, 0x10);
    std::memcpy(p + 0x12, reinterpret_cast<std::uint8_t*>(self) + 0x12, 0x40);
    SetVtable(p, chunk::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BD30(Message* self) {   // 00416780
    SetVtable(self, chunk::kVtable);
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
    if (!self) return;
    F<std::uint32_t>(self, 8) = 1;
    F<std::uint32_t>(self, 0xC) = 0;
    SetVtable(self, chunk::kVtable);
}
void __fastcall MessageWrite73BD30(Message* self, void*, char** cursor) {   // 00416800
    PutObject(cursor, F<void*>(self, 0xC));
    Put(cursor, F<std::uint16_t>(self, 0x10));
    std::memcpy(*cursor, reinterpret_cast<std::uint8_t*>(self) + 0x12, 0x40);
    *cursor += 0x40;
}
void __fastcall MessageRead73BD30(Message* self, void*, char** cursor) {   // 00416850
    TakeObject(cursor, self, 0xC);
    F<std::uint16_t>(self, 0x10) = Take<std::uint16_t>(cursor);
    std::memcpy(reinterpret_cast<std::uint8_t*>(self) + 0x12, *cursor, 0x40);
    *cursor += 0x40;
}
void __fastcall MessageDestruct73BD30(Message* self) {   // 004168E0
    SetVtable(self, chunk::kVtable);
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BD48 (0x24, type 14): an award: connection +0xC (counted, not sent), kind +0x10, value
// +0x14, text +0x18.
namespace award {
constexpr std::uintptr_t kVtable = 0x0073BD48;
}
Message* __fastcall MessageFactory73BD48() {   // 00416940
    std::uint8_t* p = NewMessage(0x24);
    if (!p) return nullptr;
    FreshHeader(p);
    F<std::uint32_t>(p, 0xC) = 0;
    StringEmpty(S(p, 0x18));
    SetVtable(p, award::kVtable);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BD48(Message* self) {   // 00416A10
    std::uint8_t* p = NewMessage(0x24);
    if (!p) return nullptr;
    CloneHeader(p, self);
    CopyCounted(p, 0xC, self);
    F<std::uint32_t>(p, 0x10) = F<std::uint32_t>(self, 0x10);
    F<std::uint32_t>(p, 0x14) = F<std::uint32_t>(self, 0x14);
    StringCopy(S(p, 0x18), S(self, 0x18));
    SetVtable(p, award::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BD48(Message* self) {   // 00416B30
    SetVtable(self, award::kVtable);
    StringFree(S(self, 0x18));
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
    if (!self) return;
    FreshHeader(self);
    F<std::uint32_t>(self, 0xC) = 0;
    StringEmpty(S(self, 0x18));
    SetVtable(self, award::kVtable);
}
void __fastcall MessageWrite73BD48(Message* self, void*, char** cursor) {   // 00416C00
    Put(cursor, F<std::uint32_t>(self, 0x10));
    Put(cursor, F<std::uint32_t>(self, 0x14));
    PutString(cursor, S(self, 0x18));
}
void __fastcall MessageRead73BD48(Message* self, void*, char** cursor) {   // 00416C60
    F<std::uint32_t>(self, 0x10) = Take<std::uint32_t>(cursor);
    F<std::uint32_t>(self, 0x14) = Take<std::uint32_t>(cursor);
    TakeString(cursor, S(self, 0x18));
}
void __fastcall MessageDestruct73BD48(Message* self) {   // 00416D70
    SetVtable(self, award::kVtable);
    StringFree(S(self, 0x18));
    ReleaseCounted(self, 0xC);
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BD60 (0x28, type 17): a trade: players +0xC / +0x10, offers +0x14 / +0x18 (counted), done
// +0x1C (byte), values +0x20 / +0x24. The offers, done and values are sent.
namespace trade {
constexpr std::uintptr_t kVtable = 0x0073BD60;
void Clear(std::uint8_t* p) {
    F<std::uint32_t>(p, 0xC) = 0;
    F<std::uint32_t>(p, 0x10) = 0;
    F<std::uint32_t>(p, 0x14) = 0;
    F<std::uint32_t>(p, 0x18) = 0;
    SetVtable(p, kVtable);
    p[0x1C] = 0;
    F<std::uint32_t>(p, 0x20) = 0;
    F<std::uint32_t>(p, 0x24) = 0;
}
void Release(Message* self) {
    ReleaseCounted(self, 0x18);
    ReleaseCounted(self, 0x14);
    ReleaseCounted(self, 0x10);
    ReleaseCounted(self, 0xC);
}
} // namespace trade

Message* __fastcall MessageFactory73BD60() {   // 00416DF0
    std::uint8_t* p = NewMessage(0x28);
    if (!p) return nullptr;
    F<std::uint32_t>(p, 8) = 1;
    trade::Clear(p);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BD60(Message* self) {   // 00416E30
    std::uint8_t* p = NewMessage(0x28);
    if (!p) return nullptr;
    CloneHeader(p, self);
    CopyCounted(p, 0xC, self);
    CopyCounted(p, 0x10, self);
    CopyCounted(p, 0x14, self);
    CopyCounted(p, 0x18, self);
    p[0x1C] = F<std::uint8_t>(self, 0x1C);
    F<std::uint32_t>(p, 0x20) = F<std::uint32_t>(self, 0x20);
    F<std::uint32_t>(p, 0x24) = F<std::uint32_t>(self, 0x24);
    SetVtable(p, trade::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BD60(Message* self) {   // 00416EB0
    SetVtable(self, trade::kVtable);
    trade::Release(self);
    SetVtable(self, kRootVtable);
    if (!self) return;
    F<std::uint32_t>(self, 8) = 1;
    trade::Clear(reinterpret_cast<std::uint8_t*>(self));
}
void __fastcall MessageWrite73BD60(Message* self, void*, char** cursor) {   // 00412C80
    PutObject(cursor, F<void*>(self, 0x14));
    PutObject(cursor, F<void*>(self, 0x18));
    Put(cursor, F<std::uint8_t>(self, 0x1C));
    Put(cursor, F<std::uint32_t>(self, 0x20));
    Put(cursor, F<std::uint32_t>(self, 0x24));
}
void __fastcall MessageRead73BD60(Message* self, void*, char** cursor) {   // 00412CE0
    TakeObject(cursor, self, 0x14);
    TakeObject(cursor, self, 0x18);
    F<std::uint8_t>(self, 0x1C) = Take<std::uint8_t>(cursor);
    F<std::uint32_t>(self, 0x20) = Take<std::uint32_t>(cursor);
    F<std::uint32_t>(self, 0x24) = Take<std::uint32_t>(cursor);
}
void __fastcall MessageDestruct73BD60(Message* self) {   // 00416FC0
    SetVtable(self, trade::kVtable);
    trade::Release(self);
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BD78 (0x34, type 16): a party member: a CMemoryStream at +0xC (data +0x24, size +0x30).
namespace member {
constexpr std::uintptr_t kVtable = 0x0073BD78;
void InitStream(std::uint8_t* p) {
    F<std::uint32_t>(p, 0x14) = 2000000000;
    SetVtable(p + 0xC, kMemoryStreamVtable);
    F<std::uint32_t>(p, 0x24) = 0;
    F<std::uint32_t>(p, 0x28) = 0;
    F<std::uint32_t>(p, 0x2C) = 0;
    F<std::uint32_t>(p, 0x30) = 0;
    p[0x11] = 1;
    F<std::uint32_t>(p, 0x1C) = 0x80;
    F<std::uint32_t>(p, 0x20) = 0xFFFFFFFFu;
}
void FreeStream(Message* self) {
    void* data = F<void*>(self, 0x24);
    SetVtable(reinterpret_cast<std::uint8_t*>(self) + 0xC, kMemoryStreamVtable);
    if (data) gamecrt::operator_delete(data);
    SetVtable(reinterpret_cast<std::uint8_t*>(self) + 0xC, kStreamVtable);
}
} // namespace member

Message* __fastcall MessageFactory73BD78() {   // 00417300
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    F<std::uint32_t>(p, 8) = 1;
    member::InitStream(p);
    SetVtable(p, member::kVtable);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BD78(Message* self) {   // 00417420
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    CloneHeader(p, self);
    std::uint8_t* stream = p + 0xC;
    F<std::uint32_t>(stream, 8) = 2000000000;
    SetVtable(stream, kMemoryStreamVtable);
    F<std::uint32_t>(stream, 0x18) = 0;
    F<std::uint32_t>(stream, 0x1C) = 0;
    F<std::uint32_t>(stream, 0x20) = 0;
    F<std::uint32_t>(stream, 0x24) = 0;
    stream[5] = 1;
    F<std::uint32_t>(stream, 0x10) = 0x80;
    F<std::uint32_t>(stream, 0x14) = 0xFFFFFFFFu;
    StreamReserve(stream, nullptr, F<std::uint32_t>(self, 0x30));
    ei::VCall<int>(stream, 1, F<void*>(self, 0x24), F<std::uint32_t>(stream, 0x24));
    SetVtable(p, member::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BD78(Message* self) {   // 004174F0
    SetVtable(self, member::kVtable);
    member::FreeStream(self);
    SetVtable(self, kRootVtable);
    if (!self) return;
    F<std::uint32_t>(self, 8) = 1;
    member::InitStream(reinterpret_cast<std::uint8_t*>(self));
    SetVtable(self, member::kVtable);
}
void __fastcall MessageWrite73BD78(Message* self, void*, char** cursor) {   // 00417560
    std::uint32_t size = ei::VCall<std::uint32_t>(reinterpret_cast<std::uint8_t*>(self) + 0xC, 4);
    Put(cursor, static_cast<std::uint16_t>(size));
    std::size_t n = size & 0xFFFF;
    std::memcpy(*cursor, F<void*>(self, 0x24), n);
    *cursor += n;
}
void __fastcall MessageRead73BD78(Message* self, void*, char** cursor) {   // 004175B0
    std::size_t n = Take<std::uint16_t>(cursor);
    StreamResize(reinterpret_cast<std::uint8_t*>(self) + 0xC, nullptr, static_cast<std::uint32_t>(n));
    std::memcpy(F<void*>(self, 0x24), *cursor, n);
    *cursor += n;
}
void __fastcall MessageDestruct73BD78(Message* self) {   // 00417620
    SetVtable(self, member::kVtable);
    member::FreeStream(self);
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BDD0 (0x34, type 15): objects +0xC (vector of counted pointers), +0x18, +0x1C (byte), +0x20,
// +0x24, two counted pointers +0x28 / +0x2C (all sent), +0x30 (byte, not sent).
namespace objects {
constexpr std::uintptr_t kVtable = 0x0073BDD0;
struct Refs {
    RefPtr* start;
    RefPtr* finish;
    RefPtr* eos;
};
Refs* List(void* self) { return &F<Refs>(self, 0xC); }
void Init(std::uint8_t* p) {
    Refs* list = List(p);
    list->start = nullptr;
    list->finish = nullptr;
    list->eos = nullptr;
    F<std::uint32_t>(p, 0x28) = 0;
    F<std::uint32_t>(p, 0x2C) = 0;
    SetVtable(p, kVtable);
    F<std::uint32_t>(p, 0x20) = 0;
    F<std::uint32_t>(p, 0x24) = 1;
    p[0x1C] = 0;
    F<std::uint32_t>(p, 0x18) = 0;
    p[0x30] = 0;
}
} // namespace objects

Message* __fastcall MessageFactory73BDD0() {   // 00417650
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    F<std::uint32_t>(p, 8) = 1;
    objects::Init(p);
    return reinterpret_cast<Message*>(p);
}
Message* __fastcall MessageClone73BDD0(Message* self) {   // 004176A0
    std::uint8_t* p = NewMessage(0x34);
    if (!p) return nullptr;
    CloneHeader(p, self);
    objects::Refs* from = objects::List(self);
    objects::Refs* to = objects::List(p);
    std::uint8_t allocator = 0;
    const void* copy = RefsGetAllocator(from, nullptr, &allocator);
    RefsBaseConstruct(to, nullptr, static_cast<std::size_t>(from->finish - from->start), copy);
    to->finish = RefsCopy(from->start, from->finish, to->start, 0);
    F<std::uint32_t>(p, 0x18) = F<std::uint32_t>(self, 0x18);
    p[0x1C] = F<std::uint8_t>(self, 0x1C);
    F<std::uint32_t>(p, 0x20) = F<std::uint32_t>(self, 0x20);
    F<std::uint32_t>(p, 0x24) = F<std::uint32_t>(self, 0x24);
    CopyCounted(p, 0x28, self);
    CopyCounted(p, 0x2C, self);
    SetVtable(p, objects::kVtable);
    p[0x30] = F<std::uint8_t>(self, 0x30);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BDD0(Message* self) {   // 004177A0
    SetVtable(self, objects::kVtable);
    ReleaseCounted(self, 0x2C);
    ReleaseCounted(self, 0x28);
    objects::Refs* list = objects::List(self);
    for (RefPtr *p = list->start, *end = list->finish; p != end; ++p) RefRelease(p, nullptr, 0);
    RefsDeallocate(&list->eos, nullptr, list->start, static_cast<std::size_t>(list->eos - list->start));
    SetVtable(self, kRootVtable);
    if (!self) return;
    F<std::uint32_t>(self, 8) = 1;
    objects::Init(reinterpret_cast<std::uint8_t*>(self));
}
void __fastcall MessageWrite73BDD0(Message* self, void*, char** cursor) {   // 00412AC0
    objects::Refs* list = objects::List(self);
    std::int32_t count = static_cast<std::int32_t>(list->finish - list->start);
    Put(cursor, count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, objects::List(self)->start[i].ptr);
    Put(cursor, F<std::uint32_t>(self, 0x18));
    Put(cursor, F<std::uint8_t>(self, 0x1C));
    Put(cursor, F<std::uint32_t>(self, 0x20));
    Put(cursor, F<std::uint32_t>(self, 0x24));
    PutObject(cursor, F<void*>(self, 0x28));
    PutObject(cursor, F<void*>(self, 0x2C));
}
void __fastcall MessageRead73BDD0(Message* self, void*, char** cursor) {   // 00412B70
    std::int32_t count = Take<std::int32_t>(cursor);
    RefsResize(objects::List(self), nullptr, static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        RefPtr* slot = objects::List(self)->start + i;
        AssignCounted(slot, 0, static_cast<CRefObject*>(ConnReadObjectRef(Connection(), nullptr, cursor)));
    }
    F<std::uint32_t>(self, 0x18) = Take<std::uint32_t>(cursor);
    F<std::uint8_t>(self, 0x1C) = Take<std::uint8_t>(cursor);
    F<std::uint32_t>(self, 0x20) = Take<std::uint32_t>(cursor);
    F<std::uint32_t>(self, 0x24) = Take<std::uint32_t>(cursor);
    TakeObject(cursor, self, 0x28);
    TakeObject(cursor, self, 0x2C);
}
void __fastcall MessageDestruct73BDD0(Message* self) {   // 004178F0
    SetVtable(self, objects::kVtable);
    ReleaseCounted(self, 0x2C);
    ReleaseCounted(self, 0x28);
    objects::Refs* list = objects::List(self);
    for (RefPtr *p = list->start, *end = list->finish; p != end; ++p)
        if (p->ptr) p->ptr->Release();
    if (list->start) NodeFree(list->start, static_cast<std::size_t>(list->eos - list->start) * 4);
    SetVtable(self, kRootVtable);
}

// ================================================================================================
// 0073BB28 (0x48): a server entry (the lobby's description of a game; made by 0041A7E0 and
// 0041D420, read from the server list): the base 0073BB40 (an address and the fields +0x18 ..
// +0x24, constructor 004413E0), name +0x28, kind +0x34, description +0x38, players +0x44, maximum
// +0x45, flag +0x46. Its deleting destructor is 004128C0; slot 1 is the empty 00412580.
namespace entry {
constexpr std::uintptr_t kVtable = 0x0073BB28;
constexpr std::uintptr_t kBaseVtable = 0x0073BB40;
constexpr std::uintptr_t kEmptyText = 0x00783D6C;
} // namespace entry

namespace {
__declspec(naked) void __fastcall EntryConstructBase(void* /*entry*/) EI_ORIGINAL(0x004413E0)
__declspec(naked) void __fastcall StringDestruct(StringRep* /*s*/) EI_ORIGINAL(0x00425040)
void AssignEmptyText(StringRep* s) {
    const char* text = reinterpret_cast<const char*>(entry::kEmptyText);
    StringAssignRange(s, nullptr, text, text + std::strlen(text));
}
} // namespace

Message* __fastcall MessageClone73BB28(Message* self) {   // 00412590
    std::uint8_t* p = NewMessage(0x48);
    if (!p) return nullptr;
    SetVtable(p, kRefValueVtable);
    for (std::size_t offset : {0x08u, 0x0Cu, 0x10u, 0x14u, 0x18u, 0x1Cu}) F<std::uint32_t>(p, offset) = F<std::uint32_t>(self, offset);
    p[0x20] = F<std::uint8_t>(self, 0x20);
    F<std::uint32_t>(p, 0x24) = F<std::uint32_t>(self, 0x24);
    SetVtable(p, entry::kBaseVtable);
    StringCopy(S(p, 0x28), S(self, 0x28));
    F<std::uint32_t>(p, 0x34) = F<std::uint32_t>(self, 0x34);
    StringCopy(S(p, 0x38), S(self, 0x38));
    p[0x44] = F<std::uint8_t>(self, 0x44);
    p[0x45] = F<std::uint8_t>(self, 0x45);
    p[0x46] = F<std::uint8_t>(self, 0x46);
    SetVtable(p, entry::kVtable);
    return reinterpret_cast<Message*>(p);
}
void __fastcall MessageReset73BB28(Message* self) {   // 00412750
    SetVtable(self, entry::kVtable);
    StringDestruct(S(self, 0x38));
    StringDestruct(S(self, 0x28));
    SetVtable(self, kRootVtable);
    if (!self) return;
    EntryConstructBase(self);
    StringEmpty(S(self, 0x28));
    StringEmpty(S(self, 0x38));
    SetVtable(self, entry::kVtable);
    AssignEmptyText(S(self, 0x28));
    F<std::uint32_t>(self, 0x34) = 4;
    AssignEmptyText(S(self, 0x38));
    F<std::uint8_t>(self, 0x44) = 0;
    F<std::uint8_t>(self, 0x45) = 0;
    F<std::uint8_t>(self, 0x46) = 0;
}
void __fastcall MessageWrite73BB28(Message* self, void*, char** cursor) {   // 00412990
    PutString(cursor, S(self, 0x28));
    Put(cursor, F<std::uint32_t>(self, 0x34));
    PutString(cursor, S(self, 0x38));
    Put(cursor, F<std::uint8_t>(self, 0x44));
    Put(cursor, F<std::uint8_t>(self, 0x45));
    Put(cursor, F<std::uint8_t>(self, 0x46));
}
void __fastcall MessageRead73BB28(Message* self, void*, char** cursor) {   // 00412A30
    TakeString(cursor, S(self, 0x28));
    F<std::uint32_t>(self, 0x34) = Take<std::uint32_t>(cursor);
    TakeString(cursor, S(self, 0x38));
    F<std::uint8_t>(self, 0x44) = Take<std::uint8_t>(cursor);
    F<std::uint8_t>(self, 0x45) = Take<std::uint8_t>(cursor);
    F<std::uint8_t>(self, 0x46) = Take<std::uint8_t>(cursor);
}
void __fastcall MessageDestruct73BB28(Message* self) {   // 004128E0
    SetVtable(self, entry::kVtable);
    StringFree(S(self, 0x38));
    StringFree(S(self, 0x28));
    SetVtable(self, kRootVtable);
}

EI_REPLACE(0x00414DB0, MessageFactory73BCA0);
EI_REPLACE(0x00414E80, MessageClone73BCA0);
EI_REPLACE(0x00414FA0, MessageReset73BCA0);
EI_REPLACE(0x004150B0, MessageWrite73BCA0);
EI_REPLACE(0x00415120, MessageRead73BCA0);
EI_REPLACE(0x004152A0, MessageDestruct73BCA0);
EI_REPLACE(0x004171A0, MessageFactory73BC28);
EI_REPLACE(0x00413930, MessageClone73BC28);
EI_REPLACE(0x00413A50, MessageReset73BC28);
EI_REPLACE(0x00413B10, MessageWrite73BC28);
EI_REPLACE(0x00413B50, MessageRead73BC28);
EI_REPLACE(0x00413C40, MessageDestruct73BC28);
EI_REPLACE(0x00415570, MessageFactory73BCD0);
EI_REPLACE(0x00415620, MessageClone73BCD0);
EI_REPLACE(0x00415740, MessageReset73BCD0);
EI_REPLACE(0x00415800, MessageWrite73BCD0);
EI_REPLACE(0x00415840, MessageRead73BCD0);
EI_REPLACE(0x00415930, MessageDestruct73BCD0);
EI_REPLACE(0x00415D50, MessageFactory73BD00);
EI_REPLACE(0x00415E00, MessageClone73BD00);
EI_REPLACE(0x00415F20, MessageReset73BD00);
EI_REPLACE(0x00415FE0, MessageWrite73BD00);
EI_REPLACE(0x00416020, MessageRead73BD00);
EI_REPLACE(0x00416110, MessageDestruct73BD00);
EI_REPLACE(0x00417250, MessageFactory73BC40);
EI_REPLACE(0x00413D20, MessageClone73BC40);
EI_REPLACE(0x00413E40, MessageReset73BC40);
EI_REPLACE(0x00413F00, MessageWrite73BC40);
EI_REPLACE(0x00413F50, MessageRead73BC40);
EI_REPLACE(0x00414060, MessageDestruct73BC40);
EI_REPLACE(0x00415980, MessageFactory73BCE8);
EI_REPLACE(0x00415A30, MessageClone73BCE8);
EI_REPLACE(0x00415B60, MessageReset73BCE8);
EI_REPLACE(0x00415C20, MessageWrite73BCE8);
EI_REPLACE(0x00415C80, MessageRead73BCE8);
EI_REPLACE(0x00415D00, MessageDestruct73BCE8);
EI_REPLACE(0x00415320, MessageFactory73BCB8);
EI_REPLACE(0x00415350, MessageClone73BCB8);
EI_REPLACE(0x004153B0, MessageReset73BCB8);
EI_REPLACE(0x00415450, MessageWrite73BCB8);
EI_REPLACE(0x00415470, MessageRead73BCB8);
EI_REPLACE(0x004154D0, MessageDestruct73BCB8);
EI_REPLACE(0x004179E0, MessageFactory73BC58);
EI_REPLACE(0x004140D0, MessageClone73BC58);
EI_REPLACE(0x00414130, MessageReset73BC58);
EI_REPLACE(0x004141F0, MessageWrite73BC58);
EI_REPLACE(0x00414210, MessageRead73BC58);
EI_REPLACE(0x00414270, MessageDestruct73BC58);
EI_REPLACE(0x00414420, MessageFactory73BC70);
EI_REPLACE(0x00414500, MessageClone73BC70);
EI_REPLACE(0x00414650, MessageReset73BC70);
EI_REPLACE(0x00414770, MessageWrite73BC70);
EI_REPLACE(0x00414820, MessageRead73BC70);
EI_REPLACE(0x004148E0, MessageDestruct73BC70);
EI_REPLACE(0x00414960, MessageFactory73BC88);
EI_REPLACE(0x00414A00, MessageClone73BC88);
EI_REPLACE(0x00414AC0, MessageReset73BC88);
EI_REPLACE(0x00414BB0, MessageWrite73BC88);
EI_REPLACE(0x00414C00, MessageRead73BC88);
EI_REPLACE(0x00414D10, MessageDestruct73BC88);
EI_REPLACE(0x004170A0, MessageFactory73BBF8);
EI_REPLACE(0x00413440, MessageClone73BBF8);
EI_REPLACE(0x00413570, MessageReset73BBF8);
EI_REPLACE(0x004136B0, MessageWrite73BBF8);
EI_REPLACE(0x00413750, MessageRead73BBF8);
EI_REPLACE(0x00413800, MessageDestruct73BBF8);
EI_REPLACE(0x00416160, MessageFactory73BD18);
EI_REPLACE(0x00416220, MessageClone73BD18);
EI_REPLACE(0x00416390, MessageReset73BD18);
EI_REPLACE(0x004164D0, MessageWrite73BD18);
EI_REPLACE(0x00416530, MessageRead73BD18);
EI_REPLACE(0x00416640, MessageDestruct73BD18);
EI_REPLACE(0x004166F0, MessageFactory73BD30);
EI_REPLACE(0x00416720, MessageClone73BD30);
EI_REPLACE(0x00416780, MessageReset73BD30);
EI_REPLACE(0x00416800, MessageWrite73BD30);
EI_REPLACE(0x00416850, MessageRead73BD30);
EI_REPLACE(0x004168E0, MessageDestruct73BD30);
EI_REPLACE(0x00416940, MessageFactory73BD48);
EI_REPLACE(0x00416A10, MessageClone73BD48);
EI_REPLACE(0x00416B30, MessageReset73BD48);
EI_REPLACE(0x00416C00, MessageWrite73BD48);
EI_REPLACE(0x00416C60, MessageRead73BD48);
EI_REPLACE(0x00416D70, MessageDestruct73BD48);
EI_REPLACE(0x00416DF0, MessageFactory73BD60);
EI_REPLACE(0x00416E30, MessageClone73BD60);
EI_REPLACE(0x00416EB0, MessageReset73BD60);
EI_REPLACE(0x00412C80, MessageWrite73BD60);
EI_REPLACE(0x00412CE0, MessageRead73BD60);
EI_REPLACE(0x00416FC0, MessageDestruct73BD60);
EI_REPLACE(0x00417300, MessageFactory73BD78);
EI_REPLACE(0x00417420, MessageClone73BD78);
EI_REPLACE(0x004174F0, MessageReset73BD78);
EI_REPLACE(0x00417560, MessageWrite73BD78);
EI_REPLACE(0x004175B0, MessageRead73BD78);
EI_REPLACE(0x00417620, MessageDestruct73BD78);
EI_REPLACE(0x00417650, MessageFactory73BDD0);
EI_REPLACE(0x004176A0, MessageClone73BDD0);
EI_REPLACE(0x004177A0, MessageReset73BDD0);
EI_REPLACE(0x00412AC0, MessageWrite73BDD0);
EI_REPLACE(0x00412B70, MessageRead73BDD0);
EI_REPLACE(0x004178F0, MessageDestruct73BDD0);

EI_REPLACE(0x00412590, MessageClone73BB28);
EI_REPLACE(0x00412750, MessageReset73BB28);
EI_REPLACE(0x00412990, MessageWrite73BB28);
EI_REPLACE(0x00412A30, MessageRead73BB28);
EI_REPLACE(0x004128E0, MessageDestruct73BB28);

} // namespace net
