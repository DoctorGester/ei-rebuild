// Checks for net_message_classes.cpp.
//  - Write / read of the classes without object references: side by side. Write: the original
//    into the cursor's buffer, then ours over it; the bytes and the end cursor must agree. Read: the
//    original reads into the message, its fields are captured through our write, then ours reads
//    the same bytes again; fields and end cursor must agree.
//  - Factories, clones, resets, destructor bodies and the classes with object references: ours
//    only (they allocate, release references or record objects in the connection), with the
//    vtable, version and reference count checked.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"

#include <cstring>
#include <vector>

namespace net {
Message* __fastcall MessageFactory73BC70();
Message* __fastcall MessageClone73BC70(Message* self);
void __fastcall MessageReset73BC70(Message* self);
void __fastcall MessageWrite73BC70(Message* self, void*, char** cursor);
void __fastcall MessageRead73BC70(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BC70(Message* self);
Message* __fastcall MessageFactory73BC88();
Message* __fastcall MessageClone73BC88(Message* self);
void __fastcall MessageReset73BC88(Message* self);
void __fastcall MessageWrite73BC88(Message* self, void*, char** cursor);
void __fastcall MessageRead73BC88(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BC88(Message* self);
Message* __fastcall MessageFactory73BCA0();
Message* __fastcall MessageClone73BCA0(Message* self);
void __fastcall MessageReset73BCA0(Message* self);
void __fastcall MessageWrite73BCA0(Message* self, void*, char** cursor);
void __fastcall MessageRead73BCA0(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BCA0(Message* self);
Message* __fastcall MessageFactory73BC28();
Message* __fastcall MessageClone73BC28(Message* self);
void __fastcall MessageReset73BC28(Message* self);
void __fastcall MessageWrite73BC28(Message* self, void*, char** cursor);
void __fastcall MessageRead73BC28(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BC28(Message* self);
Message* __fastcall MessageFactory73BCD0();
Message* __fastcall MessageClone73BCD0(Message* self);
void __fastcall MessageReset73BCD0(Message* self);
void __fastcall MessageWrite73BCD0(Message* self, void*, char** cursor);
void __fastcall MessageRead73BCD0(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BCD0(Message* self);
Message* __fastcall MessageFactory73BD00();
Message* __fastcall MessageClone73BD00(Message* self);
void __fastcall MessageReset73BD00(Message* self);
void __fastcall MessageWrite73BD00(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD00(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD00(Message* self);
Message* __fastcall MessageFactory73BC40();
Message* __fastcall MessageClone73BC40(Message* self);
void __fastcall MessageReset73BC40(Message* self);
void __fastcall MessageWrite73BC40(Message* self, void*, char** cursor);
void __fastcall MessageRead73BC40(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BC40(Message* self);
Message* __fastcall MessageFactory73BCE8();
Message* __fastcall MessageClone73BCE8(Message* self);
void __fastcall MessageReset73BCE8(Message* self);
void __fastcall MessageWrite73BCE8(Message* self, void*, char** cursor);
void __fastcall MessageRead73BCE8(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BCE8(Message* self);
Message* __fastcall MessageFactory73BBF8();
Message* __fastcall MessageClone73BBF8(Message* self);
void __fastcall MessageReset73BBF8(Message* self);
void __fastcall MessageWrite73BBF8(Message* self, void*, char** cursor);
void __fastcall MessageRead73BBF8(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BBF8(Message* self);
Message* __fastcall MessageFactory73BCB8();
Message* __fastcall MessageClone73BCB8(Message* self);
void __fastcall MessageReset73BCB8(Message* self);
void __fastcall MessageWrite73BCB8(Message* self, void*, char** cursor);
void __fastcall MessageRead73BCB8(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BCB8(Message* self);
Message* __fastcall MessageFactory73BC58();
Message* __fastcall MessageClone73BC58(Message* self);
void __fastcall MessageReset73BC58(Message* self);
void __fastcall MessageWrite73BC58(Message* self, void*, char** cursor);
void __fastcall MessageRead73BC58(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BC58(Message* self);
Message* __fastcall MessageFactory73BD18();
Message* __fastcall MessageClone73BD18(Message* self);
void __fastcall MessageReset73BD18(Message* self);
void __fastcall MessageWrite73BD18(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD18(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD18(Message* self);
Message* __fastcall MessageFactory73BD30();
Message* __fastcall MessageClone73BD30(Message* self);
void __fastcall MessageReset73BD30(Message* self);
void __fastcall MessageWrite73BD30(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD30(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD30(Message* self);
Message* __fastcall MessageFactory73BD48();
Message* __fastcall MessageClone73BD48(Message* self);
void __fastcall MessageReset73BD48(Message* self);
void __fastcall MessageWrite73BD48(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD48(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD48(Message* self);
Message* __fastcall MessageFactory73BD60();
Message* __fastcall MessageClone73BD60(Message* self);
void __fastcall MessageReset73BD60(Message* self);
void __fastcall MessageWrite73BD60(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD60(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD60(Message* self);
Message* __fastcall MessageFactory73BD78();
Message* __fastcall MessageClone73BD78(Message* self);
void __fastcall MessageReset73BD78(Message* self);
void __fastcall MessageWrite73BD78(Message* self, void*, char** cursor);
void __fastcall MessageRead73BD78(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BD78(Message* self);
Message* __fastcall MessageFactory73BDD0();
Message* __fastcall MessageClone73BDD0(Message* self);
void __fastcall MessageReset73BDD0(Message* self);
void __fastcall MessageWrite73BDD0(Message* self, void*, char** cursor);
void __fastcall MessageRead73BDD0(Message* self, void*, char** cursor);
void __fastcall MessageDestruct73BDD0(Message* self);
} // namespace net

namespace {
void* s_unused;

using WriteFn = void(__fastcall*)(net::Message*, void*, char**);
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

void CheckWrite(const char* name, WriteFn original, WriteFn ours, net::Message* self, char** cursor) {
    char* start = *cursor;
    char* expectedEnd = start;
    original(self, nullptr, &expectedEnd);
    std::vector<char> expected(start, expectedEnd);
    ours(self, nullptr, cursor);
    bool ok = *cursor == expectedEnd && (expected.empty() || std::memcmp(start, expected.data(), expected.size()) == 0);
    ei::verify::Check(name, true, ok, "%d bytes", static_cast<int>(expected.size()));
}

void CheckRead(const char* name, WriteFn original, WriteFn ours, WriteFn capture, net::Message* self, char** cursor) {
    char* start = *cursor;
    char* expectedEnd = start;
    original(self, nullptr, &expectedEnd);
    std::size_t used = static_cast<std::size_t>(expectedEnd - start);
    std::vector<char> expected(used + 0x100), actual(used + 0x100);
    char* p = expected.data();
    capture(self, nullptr, &p);
    std::size_t expectedSize = static_cast<std::size_t>(p - expected.data());
    ours(self, nullptr, cursor);
    p = actual.data();
    capture(self, nullptr, &p);
    std::size_t actualSize = static_cast<std::size_t>(p - actual.data());
    bool ok = *cursor == expectedEnd && expectedSize == actualSize &&
              std::memcmp(expected.data(), actual.data(), expectedSize) == 0;
    ei::verify::Check(name, true, ok, "%d bytes", static_cast<int>(used));
}

void CheckMade(const char* name, net::Message* made, std::uintptr_t vtable, std::uint32_t version) {
    bool ok = !made || (VtableOf(made) == vtable && Dword(made, 8) == version && (Dword(made, 4) & 0xFFFF) == 0);
    ei::verify::Check(name, true, ok, "");
}

// 73BC70
net::Message* __fastcall Factory73BC70Verify() {
    net::Message* made = net::MessageFactory73BC70();
    CheckMade("MessageFactory73BC70", made, 0x0073BC70, 1);
    return made;
}
net::Message* __fastcall Clone73BC70Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BC70(self);
    CheckMade("MessageClone73BC70", made, 0x0073BC70, Dword(self, 8));
    return made;
}
void __fastcall Reset73BC70Verify(net::Message* self) {
    net::MessageReset73BC70(self);
    ei::verify::Check("MessageReset73BC70", true, VtableOf(self) == 0x0073BC70 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BC70Verify(net::Message* self) {
    net::MessageDestruct73BC70(self);
    ei::verify::Check("MessageDestruct73BC70", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BC70, s_read73BC70;
void __fastcall Write73BC70Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BC70", s_write73BC70, net::MessageWrite73BC70, self, cursor);
}
void __fastcall Read73BC70Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BC70", s_read73BC70, net::MessageRead73BC70, net::MessageWrite73BC70, self, cursor);
}
// 73BC88
net::Message* __fastcall Factory73BC88Verify() {
    net::Message* made = net::MessageFactory73BC88();
    CheckMade("MessageFactory73BC88", made, 0x0073BC88, 1);
    return made;
}
net::Message* __fastcall Clone73BC88Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BC88(self);
    CheckMade("MessageClone73BC88", made, 0x0073BC88, Dword(self, 8));
    return made;
}
void __fastcall Reset73BC88Verify(net::Message* self) {
    net::MessageReset73BC88(self);
    ei::verify::Check("MessageReset73BC88", true, VtableOf(self) == 0x0073BC88 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BC88Verify(net::Message* self) {
    net::MessageDestruct73BC88(self);
    ei::verify::Check("MessageDestruct73BC88", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BC88, s_read73BC88;
void __fastcall Write73BC88Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BC88", s_write73BC88, net::MessageWrite73BC88, self, cursor);
}
void __fastcall Read73BC88Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BC88", s_read73BC88, net::MessageRead73BC88, net::MessageWrite73BC88, self, cursor);
}
// 73BCA0
net::Message* __fastcall Factory73BCA0Verify() {
    net::Message* made = net::MessageFactory73BCA0();
    CheckMade("MessageFactory73BCA0", made, 0x0073BCA0, 1);
    return made;
}
net::Message* __fastcall Clone73BCA0Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BCA0(self);
    CheckMade("MessageClone73BCA0", made, 0x0073BCA0, Dword(self, 8));
    return made;
}
void __fastcall Reset73BCA0Verify(net::Message* self) {
    net::MessageReset73BCA0(self);
    ei::verify::Check("MessageReset73BCA0", true, VtableOf(self) == 0x0073BCA0 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BCA0Verify(net::Message* self) {
    net::MessageDestruct73BCA0(self);
    ei::verify::Check("MessageDestruct73BCA0", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BCA0, s_read73BCA0;
void __fastcall Write73BCA0Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BCA0", s_write73BCA0, net::MessageWrite73BCA0, self, cursor);
}
void __fastcall Read73BCA0Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BCA0", s_read73BCA0, net::MessageRead73BCA0, net::MessageWrite73BCA0, self, cursor);
}
// 73BC28
net::Message* __fastcall Factory73BC28Verify() {
    net::Message* made = net::MessageFactory73BC28();
    CheckMade("MessageFactory73BC28", made, 0x0073BC28, 1);
    return made;
}
net::Message* __fastcall Clone73BC28Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BC28(self);
    CheckMade("MessageClone73BC28", made, 0x0073BC28, Dword(self, 8));
    return made;
}
void __fastcall Reset73BC28Verify(net::Message* self) {
    net::MessageReset73BC28(self);
    ei::verify::Check("MessageReset73BC28", true, VtableOf(self) == 0x0073BC28 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BC28Verify(net::Message* self) {
    net::MessageDestruct73BC28(self);
    ei::verify::Check("MessageDestruct73BC28", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BC28, s_read73BC28;
void __fastcall Write73BC28Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BC28", s_write73BC28, net::MessageWrite73BC28, self, cursor);
}
void __fastcall Read73BC28Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BC28", s_read73BC28, net::MessageRead73BC28, net::MessageWrite73BC28, self, cursor);
}
// 73BCD0
net::Message* __fastcall Factory73BCD0Verify() {
    net::Message* made = net::MessageFactory73BCD0();
    CheckMade("MessageFactory73BCD0", made, 0x0073BCD0, 1);
    return made;
}
net::Message* __fastcall Clone73BCD0Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BCD0(self);
    CheckMade("MessageClone73BCD0", made, 0x0073BCD0, Dword(self, 8));
    return made;
}
void __fastcall Reset73BCD0Verify(net::Message* self) {
    net::MessageReset73BCD0(self);
    ei::verify::Check("MessageReset73BCD0", true, VtableOf(self) == 0x0073BCD0 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BCD0Verify(net::Message* self) {
    net::MessageDestruct73BCD0(self);
    ei::verify::Check("MessageDestruct73BCD0", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BCD0, s_read73BCD0;
void __fastcall Write73BCD0Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BCD0", s_write73BCD0, net::MessageWrite73BCD0, self, cursor);
}
void __fastcall Read73BCD0Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BCD0", s_read73BCD0, net::MessageRead73BCD0, net::MessageWrite73BCD0, self, cursor);
}
// 73BD00
net::Message* __fastcall Factory73BD00Verify() {
    net::Message* made = net::MessageFactory73BD00();
    CheckMade("MessageFactory73BD00", made, 0x0073BD00, 1);
    return made;
}
net::Message* __fastcall Clone73BD00Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD00(self);
    CheckMade("MessageClone73BD00", made, 0x0073BD00, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD00Verify(net::Message* self) {
    net::MessageReset73BD00(self);
    ei::verify::Check("MessageReset73BD00", true, VtableOf(self) == 0x0073BD00 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD00Verify(net::Message* self) {
    net::MessageDestruct73BD00(self);
    ei::verify::Check("MessageDestruct73BD00", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BD00, s_read73BD00;
void __fastcall Write73BD00Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BD00", s_write73BD00, net::MessageWrite73BD00, self, cursor);
}
void __fastcall Read73BD00Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BD00", s_read73BD00, net::MessageRead73BD00, net::MessageWrite73BD00, self, cursor);
}
// 73BC40
net::Message* __fastcall Factory73BC40Verify() {
    net::Message* made = net::MessageFactory73BC40();
    CheckMade("MessageFactory73BC40", made, 0x0073BC40, 1);
    return made;
}
net::Message* __fastcall Clone73BC40Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BC40(self);
    CheckMade("MessageClone73BC40", made, 0x0073BC40, Dword(self, 8));
    return made;
}
void __fastcall Reset73BC40Verify(net::Message* self) {
    net::MessageReset73BC40(self);
    ei::verify::Check("MessageReset73BC40", true, VtableOf(self) == 0x0073BC40 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BC40Verify(net::Message* self) {
    net::MessageDestruct73BC40(self);
    ei::verify::Check("MessageDestruct73BC40", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BC40, s_read73BC40;
void __fastcall Write73BC40Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BC40", s_write73BC40, net::MessageWrite73BC40, self, cursor);
}
void __fastcall Read73BC40Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BC40", s_read73BC40, net::MessageRead73BC40, net::MessageWrite73BC40, self, cursor);
}
// 73BCE8
net::Message* __fastcall Factory73BCE8Verify() {
    net::Message* made = net::MessageFactory73BCE8();
    CheckMade("MessageFactory73BCE8", made, 0x0073BCE8, 1);
    return made;
}
net::Message* __fastcall Clone73BCE8Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BCE8(self);
    CheckMade("MessageClone73BCE8", made, 0x0073BCE8, Dword(self, 8));
    return made;
}
void __fastcall Reset73BCE8Verify(net::Message* self) {
    net::MessageReset73BCE8(self);
    ei::verify::Check("MessageReset73BCE8", true, VtableOf(self) == 0x0073BCE8 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BCE8Verify(net::Message* self) {
    net::MessageDestruct73BCE8(self);
    ei::verify::Check("MessageDestruct73BCE8", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BCE8, s_read73BCE8;
void __fastcall Write73BCE8Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BCE8", s_write73BCE8, net::MessageWrite73BCE8, self, cursor);
}
void __fastcall Read73BCE8Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BCE8", s_read73BCE8, net::MessageRead73BCE8, net::MessageWrite73BCE8, self, cursor);
}
// 73BBF8
net::Message* __fastcall Factory73BBF8Verify() {
    net::Message* made = net::MessageFactory73BBF8();
    CheckMade("MessageFactory73BBF8", made, 0x0073BBF8, 1);
    return made;
}
net::Message* __fastcall Clone73BBF8Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BBF8(self);
    CheckMade("MessageClone73BBF8", made, 0x0073BBF8, Dword(self, 8));
    return made;
}
void __fastcall Reset73BBF8Verify(net::Message* self) {
    net::MessageReset73BBF8(self);
    ei::verify::Check("MessageReset73BBF8", true, VtableOf(self) == 0x0073BBF8 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BBF8Verify(net::Message* self) {
    net::MessageDestruct73BBF8(self);
    ei::verify::Check("MessageDestruct73BBF8", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BBF8, s_read73BBF8;
void __fastcall Write73BBF8Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BBF8", s_write73BBF8, net::MessageWrite73BBF8, self, cursor);
}
void __fastcall Read73BBF8Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BBF8", s_read73BBF8, net::MessageRead73BBF8, net::MessageWrite73BBF8, self, cursor);
}
// 73BCB8
net::Message* __fastcall Factory73BCB8Verify() {
    net::Message* made = net::MessageFactory73BCB8();
    CheckMade("MessageFactory73BCB8", made, 0x0073BCB8, 1);
    return made;
}
net::Message* __fastcall Clone73BCB8Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BCB8(self);
    CheckMade("MessageClone73BCB8", made, 0x0073BCB8, Dword(self, 8));
    return made;
}
void __fastcall Reset73BCB8Verify(net::Message* self) {
    net::MessageReset73BCB8(self);
    ei::verify::Check("MessageReset73BCB8", true, VtableOf(self) == 0x0073BCB8 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BCB8Verify(net::Message* self) {
    net::MessageDestruct73BCB8(self);
    ei::verify::Check("MessageDestruct73BCB8", true, VtableOf(self) == 0x0073BB68, "");
}
void __fastcall Write73BCB8Verify(net::Message* self, void*, char** cursor) {
    net::MessageWrite73BCB8(self, nullptr, cursor);
    ei::verify::Check("MessageWrite73BCB8", true, true, "");
}
void __fastcall Read73BCB8Verify(net::Message* self, void*, char** cursor) {
    net::MessageRead73BCB8(self, nullptr, cursor);
    ei::verify::Check("MessageRead73BCB8", true, true, "");
}
// 73BC58
net::Message* __fastcall Factory73BC58Verify() {
    net::Message* made = net::MessageFactory73BC58();
    CheckMade("MessageFactory73BC58", made, 0x0073BC58, 1);
    return made;
}
net::Message* __fastcall Clone73BC58Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BC58(self);
    CheckMade("MessageClone73BC58", made, 0x0073BC58, Dword(self, 8));
    return made;
}
void __fastcall Reset73BC58Verify(net::Message* self) {
    net::MessageReset73BC58(self);
    ei::verify::Check("MessageReset73BC58", true, VtableOf(self) == 0x0073BC58 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BC58Verify(net::Message* self) {
    net::MessageDestruct73BC58(self);
    ei::verify::Check("MessageDestruct73BC58", true, VtableOf(self) == 0x0073BB68, "");
}
void __fastcall Write73BC58Verify(net::Message* self, void*, char** cursor) {
    net::MessageWrite73BC58(self, nullptr, cursor);
    ei::verify::Check("MessageWrite73BC58", true, true, "");
}
void __fastcall Read73BC58Verify(net::Message* self, void*, char** cursor) {
    net::MessageRead73BC58(self, nullptr, cursor);
    ei::verify::Check("MessageRead73BC58", true, true, "");
}
// 73BD18
net::Message* __fastcall Factory73BD18Verify() {
    net::Message* made = net::MessageFactory73BD18();
    CheckMade("MessageFactory73BD18", made, 0x0073BD18, 1);
    return made;
}
net::Message* __fastcall Clone73BD18Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD18(self);
    CheckMade("MessageClone73BD18", made, 0x0073BD18, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD18Verify(net::Message* self) {
    net::MessageReset73BD18(self);
    ei::verify::Check("MessageReset73BD18", true, VtableOf(self) == 0x0073BD18 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD18Verify(net::Message* self) {
    net::MessageDestruct73BD18(self);
    ei::verify::Check("MessageDestruct73BD18", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BD18, s_read73BD18;
void __fastcall Write73BD18Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BD18", s_write73BD18, net::MessageWrite73BD18, self, cursor);
}
void __fastcall Read73BD18Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BD18", s_read73BD18, net::MessageRead73BD18, net::MessageWrite73BD18, self, cursor);
}
// 73BD30
net::Message* __fastcall Factory73BD30Verify() {
    net::Message* made = net::MessageFactory73BD30();
    CheckMade("MessageFactory73BD30", made, 0x0073BD30, 1);
    return made;
}
net::Message* __fastcall Clone73BD30Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD30(self);
    CheckMade("MessageClone73BD30", made, 0x0073BD30, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD30Verify(net::Message* self) {
    net::MessageReset73BD30(self);
    ei::verify::Check("MessageReset73BD30", true, VtableOf(self) == 0x0073BD30 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD30Verify(net::Message* self) {
    net::MessageDestruct73BD30(self);
    ei::verify::Check("MessageDestruct73BD30", true, VtableOf(self) == 0x0073BB68, "");
}
void __fastcall Write73BD30Verify(net::Message* self, void*, char** cursor) {
    net::MessageWrite73BD30(self, nullptr, cursor);
    ei::verify::Check("MessageWrite73BD30", true, true, "");
}
void __fastcall Read73BD30Verify(net::Message* self, void*, char** cursor) {
    net::MessageRead73BD30(self, nullptr, cursor);
    ei::verify::Check("MessageRead73BD30", true, true, "");
}
// 73BD48
net::Message* __fastcall Factory73BD48Verify() {
    net::Message* made = net::MessageFactory73BD48();
    CheckMade("MessageFactory73BD48", made, 0x0073BD48, 1);
    return made;
}
net::Message* __fastcall Clone73BD48Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD48(self);
    CheckMade("MessageClone73BD48", made, 0x0073BD48, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD48Verify(net::Message* self) {
    net::MessageReset73BD48(self);
    ei::verify::Check("MessageReset73BD48", true, VtableOf(self) == 0x0073BD48 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD48Verify(net::Message* self) {
    net::MessageDestruct73BD48(self);
    ei::verify::Check("MessageDestruct73BD48", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BD48, s_read73BD48;
void __fastcall Write73BD48Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BD48", s_write73BD48, net::MessageWrite73BD48, self, cursor);
}
void __fastcall Read73BD48Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BD48", s_read73BD48, net::MessageRead73BD48, net::MessageWrite73BD48, self, cursor);
}
// 73BD60
net::Message* __fastcall Factory73BD60Verify() {
    net::Message* made = net::MessageFactory73BD60();
    CheckMade("MessageFactory73BD60", made, 0x0073BD60, 1);
    return made;
}
net::Message* __fastcall Clone73BD60Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD60(self);
    CheckMade("MessageClone73BD60", made, 0x0073BD60, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD60Verify(net::Message* self) {
    net::MessageReset73BD60(self);
    ei::verify::Check("MessageReset73BD60", true, VtableOf(self) == 0x0073BD60 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD60Verify(net::Message* self) {
    net::MessageDestruct73BD60(self);
    ei::verify::Check("MessageDestruct73BD60", true, VtableOf(self) == 0x0073BB68, "");
}
void __fastcall Write73BD60Verify(net::Message* self, void*, char** cursor) {
    net::MessageWrite73BD60(self, nullptr, cursor);
    ei::verify::Check("MessageWrite73BD60", true, true, "");
}
void __fastcall Read73BD60Verify(net::Message* self, void*, char** cursor) {
    net::MessageRead73BD60(self, nullptr, cursor);
    ei::verify::Check("MessageRead73BD60", true, true, "");
}
// 73BD78
net::Message* __fastcall Factory73BD78Verify() {
    net::Message* made = net::MessageFactory73BD78();
    CheckMade("MessageFactory73BD78", made, 0x0073BD78, 1);
    return made;
}
net::Message* __fastcall Clone73BD78Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BD78(self);
    CheckMade("MessageClone73BD78", made, 0x0073BD78, Dword(self, 8));
    return made;
}
void __fastcall Reset73BD78Verify(net::Message* self) {
    net::MessageReset73BD78(self);
    ei::verify::Check("MessageReset73BD78", true, VtableOf(self) == 0x0073BD78 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BD78Verify(net::Message* self) {
    net::MessageDestruct73BD78(self);
    ei::verify::Check("MessageDestruct73BD78", true, VtableOf(self) == 0x0073BB68, "");
}
WriteFn s_write73BD78, s_read73BD78;
void __fastcall Write73BD78Verify(net::Message* self, void*, char** cursor) {
    CheckWrite("MessageWrite73BD78", s_write73BD78, net::MessageWrite73BD78, self, cursor);
}
void __fastcall Read73BD78Verify(net::Message* self, void*, char** cursor) {
    CheckRead("MessageRead73BD78", s_read73BD78, net::MessageRead73BD78, net::MessageWrite73BD78, self, cursor);
}
// 73BDD0
net::Message* __fastcall Factory73BDD0Verify() {
    net::Message* made = net::MessageFactory73BDD0();
    CheckMade("MessageFactory73BDD0", made, 0x0073BDD0, 1);
    return made;
}
net::Message* __fastcall Clone73BDD0Verify(net::Message* self) {
    net::Message* made = net::MessageClone73BDD0(self);
    CheckMade("MessageClone73BDD0", made, 0x0073BDD0, Dword(self, 8));
    return made;
}
void __fastcall Reset73BDD0Verify(net::Message* self) {
    net::MessageReset73BDD0(self);
    ei::verify::Check("MessageReset73BDD0", true, VtableOf(self) == 0x0073BDD0 && Dword(self, 8) == 1, "");
}
void __fastcall Destruct73BDD0Verify(net::Message* self) {
    net::MessageDestruct73BDD0(self);
    ei::verify::Check("MessageDestruct73BDD0", true, VtableOf(self) == 0x0073BB68, "");
}
void __fastcall Write73BDD0Verify(net::Message* self, void*, char** cursor) {
    net::MessageWrite73BDD0(self, nullptr, cursor);
    ei::verify::Check("MessageWrite73BDD0", true, true, "");
}
void __fastcall Read73BDD0Verify(net::Message* self, void*, char** cursor) {
    net::MessageRead73BDD0(self, nullptr, cursor);
    ei::verify::Check("MessageRead73BDD0", true, true, "");
}
} // namespace

EI_VERIFY(0x00414420, Factory73BC70Verify, 0, s_unused);
EI_VERIFY(0x00414500, Clone73BC70Verify, 0, s_unused);
EI_VERIFY(0x00414650, Reset73BC70Verify, 0, s_unused);
EI_VERIFY(0x004148E0, Destruct73BC70Verify, 0, s_unused);
EI_VERIFY(0x00414770, Write73BC70Verify, 5, s_write73BC70);
EI_VERIFY(0x00414820, Read73BC70Verify, 6, s_read73BC70);
EI_VERIFY(0x00414960, Factory73BC88Verify, 0, s_unused);
EI_VERIFY(0x00414A00, Clone73BC88Verify, 0, s_unused);
EI_VERIFY(0x00414AC0, Reset73BC88Verify, 0, s_unused);
EI_VERIFY(0x00414D10, Destruct73BC88Verify, 0, s_unused);
EI_VERIFY(0x00414BB0, Write73BC88Verify, 5, s_write73BC88);
EI_VERIFY(0x00414C00, Read73BC88Verify, 7, s_read73BC88);
EI_VERIFY(0x00414DB0, Factory73BCA0Verify, 0, s_unused);
EI_VERIFY(0x00414E80, Clone73BCA0Verify, 0, s_unused);
EI_VERIFY(0x00414FA0, Reset73BCA0Verify, 0, s_unused);
EI_VERIFY(0x004152A0, Destruct73BCA0Verify, 0, s_unused);
EI_VERIFY(0x004150B0, Write73BCA0Verify, 5, s_write73BCA0);
EI_VERIFY(0x00415120, Read73BCA0Verify, 7, s_read73BCA0);
EI_VERIFY(0x004171A0, Factory73BC28Verify, 0, s_unused);
EI_VERIFY(0x00413930, Clone73BC28Verify, 0, s_unused);
EI_VERIFY(0x00413A50, Reset73BC28Verify, 0, s_unused);
EI_VERIFY(0x00413C40, Destruct73BC28Verify, 0, s_unused);
EI_VERIFY(0x00413B10, Write73BC28Verify, 5, s_write73BC28);
EI_VERIFY(0x00413B50, Read73BC28Verify, 6, s_read73BC28);
EI_VERIFY(0x00415570, Factory73BCD0Verify, 0, s_unused);
EI_VERIFY(0x00415620, Clone73BCD0Verify, 0, s_unused);
EI_VERIFY(0x00415740, Reset73BCD0Verify, 0, s_unused);
EI_VERIFY(0x00415930, Destruct73BCD0Verify, 0, s_unused);
EI_VERIFY(0x00415800, Write73BCD0Verify, 5, s_write73BCD0);
EI_VERIFY(0x00415840, Read73BCD0Verify, 6, s_read73BCD0);
EI_VERIFY(0x00415D50, Factory73BD00Verify, 0, s_unused);
EI_VERIFY(0x00415E00, Clone73BD00Verify, 0, s_unused);
EI_VERIFY(0x00415F20, Reset73BD00Verify, 0, s_unused);
EI_VERIFY(0x00416110, Destruct73BD00Verify, 0, s_unused);
EI_VERIFY(0x00415FE0, Write73BD00Verify, 5, s_write73BD00);
EI_VERIFY(0x00416020, Read73BD00Verify, 6, s_read73BD00);
EI_VERIFY(0x00417250, Factory73BC40Verify, 0, s_unused);
EI_VERIFY(0x00413D20, Clone73BC40Verify, 0, s_unused);
EI_VERIFY(0x00413E40, Reset73BC40Verify, 0, s_unused);
EI_VERIFY(0x00414060, Destruct73BC40Verify, 0, s_unused);
EI_VERIFY(0x00413F00, Write73BC40Verify, 5, s_write73BC40);
EI_VERIFY(0x00413F50, Read73BC40Verify, 7, s_read73BC40);
EI_VERIFY(0x00415980, Factory73BCE8Verify, 0, s_unused);
EI_VERIFY(0x00415A30, Clone73BCE8Verify, 0, s_unused);
EI_VERIFY(0x00415B60, Reset73BCE8Verify, 0, s_unused);
EI_VERIFY(0x00415D00, Destruct73BCE8Verify, 0, s_unused);
EI_VERIFY(0x00415C20, Write73BCE8Verify, 5, s_write73BCE8);
EI_VERIFY(0x00415C80, Read73BCE8Verify, 6, s_read73BCE8);
EI_VERIFY(0x004170A0, Factory73BBF8Verify, 0, s_unused);
EI_VERIFY(0x00413440, Clone73BBF8Verify, 0, s_unused);
EI_VERIFY(0x00413570, Reset73BBF8Verify, 0, s_unused);
EI_VERIFY(0x00413800, Destruct73BBF8Verify, 0, s_unused);
EI_VERIFY(0x004136B0, Write73BBF8Verify, 5, s_write73BBF8);
EI_VERIFY(0x00413750, Read73BBF8Verify, 6, s_read73BBF8);
EI_VERIFY(0x00415320, Factory73BCB8Verify, 0, s_unused);
EI_VERIFY(0x00415350, Clone73BCB8Verify, 0, s_unused);
EI_VERIFY(0x004153B0, Reset73BCB8Verify, 0, s_unused);
EI_VERIFY(0x004154D0, Destruct73BCB8Verify, 0, s_unused);
EI_VERIFY(0x00415450, Write73BCB8Verify, 0, s_unused);
EI_VERIFY(0x00415470, Read73BCB8Verify, 0, s_unused);
EI_VERIFY(0x004179E0, Factory73BC58Verify, 0, s_unused);
EI_VERIFY(0x004140D0, Clone73BC58Verify, 0, s_unused);
EI_VERIFY(0x00414130, Reset73BC58Verify, 0, s_unused);
EI_VERIFY(0x00414270, Destruct73BC58Verify, 0, s_unused);
EI_VERIFY(0x004141F0, Write73BC58Verify, 0, s_unused);
EI_VERIFY(0x00414210, Read73BC58Verify, 0, s_unused);
EI_VERIFY(0x00416160, Factory73BD18Verify, 0, s_unused);
EI_VERIFY(0x00416220, Clone73BD18Verify, 0, s_unused);
EI_VERIFY(0x00416390, Reset73BD18Verify, 0, s_unused);
EI_VERIFY(0x00416640, Destruct73BD18Verify, 0, s_unused);
EI_VERIFY(0x004164D0, Write73BD18Verify, 5, s_write73BD18);
EI_VERIFY(0x00416530, Read73BD18Verify, 6, s_read73BD18);
EI_VERIFY(0x004166F0, Factory73BD30Verify, 0, s_unused);
EI_VERIFY(0x00416720, Clone73BD30Verify, 0, s_unused);
EI_VERIFY(0x00416780, Reset73BD30Verify, 0, s_unused);
EI_VERIFY(0x004168E0, Destruct73BD30Verify, 0, s_unused);
EI_VERIFY(0x00416800, Write73BD30Verify, 0, s_unused);
EI_VERIFY(0x00416850, Read73BD30Verify, 0, s_unused);
EI_VERIFY(0x00416940, Factory73BD48Verify, 0, s_unused);
EI_VERIFY(0x00416A10, Clone73BD48Verify, 0, s_unused);
EI_VERIFY(0x00416B30, Reset73BD48Verify, 0, s_unused);
EI_VERIFY(0x00416D70, Destruct73BD48Verify, 0, s_unused);
EI_VERIFY(0x00416C00, Write73BD48Verify, 5, s_write73BD48);
EI_VERIFY(0x00416C60, Read73BD48Verify, 6, s_read73BD48);
EI_VERIFY(0x00416DF0, Factory73BD60Verify, 0, s_unused);
EI_VERIFY(0x00416E30, Clone73BD60Verify, 0, s_unused);
EI_VERIFY(0x00416EB0, Reset73BD60Verify, 0, s_unused);
EI_VERIFY(0x00416FC0, Destruct73BD60Verify, 0, s_unused);
EI_VERIFY(0x00412C80, Write73BD60Verify, 0, s_unused);
EI_VERIFY(0x00412CE0, Read73BD60Verify, 0, s_unused);
EI_VERIFY(0x00417300, Factory73BD78Verify, 0, s_unused);
EI_VERIFY(0x00417420, Clone73BD78Verify, 0, s_unused);
EI_VERIFY(0x004174F0, Reset73BD78Verify, 0, s_unused);
EI_VERIFY(0x00417620, Destruct73BD78Verify, 0, s_unused);
EI_VERIFY(0x00417560, Write73BD78Verify, 5, s_write73BD78);
EI_VERIFY(0x004175B0, Read73BD78Verify, 5, s_read73BD78);
EI_VERIFY(0x00417650, Factory73BDD0Verify, 0, s_unused);
EI_VERIFY(0x004176A0, Clone73BDD0Verify, 0, s_unused);
EI_VERIFY(0x004177A0, Reset73BDD0Verify, 0, s_unused);
EI_VERIFY(0x004178F0, Destruct73BDD0Verify, 0, s_unused);
EI_VERIFY(0x00412AC0, Write73BDD0Verify, 0, s_unused);
EI_VERIFY(0x00412B70, Read73BDD0Verify, 0, s_unused);
