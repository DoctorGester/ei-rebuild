// Checks for MSVC 6's std::exception (src/lib/crt_except.cpp, S4c).
// - exception(), what(): against the original through its trampoline (the original into a scratch object;
//   the 12 bytes / the pointer).
// - the copy constructor: the original into a scratch object; the vtable, the owned flag, and the text (the
//   same pointer when not owned, an equal copy of its own when owned; the scratch copy freed).
// - operator=, ~exception, the scalar deleting destructor: ours only (they free): the vtable, the text
//   afterwards.
// The exceptions the game throws (STLport's out_of_range and length_error, by the network code and the
// containers) are not thrown in the suite, so a start-up check drives each through its address.
// _CxxThrowException has no wrapper (it does not return): tests/verify/verify_mfc_try.cpp's start-up check
// throws a CFileException* through it (the game's CStdioFile) and catches it in ours.
#include "verify.h"

#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

using ei::verify::Check;
using libcrt::Exception;

const void* ExceptionVtable() { return reinterpret_cast<const void*>(0x00747290); }

// The members through their addresses: the wrappers below once the hooks are installed.
__declspec(naked) Exception* __fastcall ConstructAt(Exception*) EI_ORIGINAL(0x006E7CD5)
__declspec(naked) Exception* __fastcall CopyConstructAt(Exception*, void*, const Exception*) EI_ORIGINAL(0x006E7D3F)
__declspec(naked) Exception* __fastcall AssignAt(Exception*, void*, const Exception*) EI_ORIGINAL(0x006E7D89)
__declspec(naked) void __fastcall DestructAt(Exception*) EI_ORIGINAL(0x006E7DA8)
__declspec(naked) Exception* __fastcall DeletingDestructAt(Exception*, void*, unsigned) EI_ORIGINAL(0x006E7CE6)
__declspec(naked) const char* __fastcall WhatAt(const Exception*) EI_ORIGINAL(0x006E7DBE)

// 006E7CD5: MOV EAX,ECX; AND dword [EAX+4],0 (6 bytes).
Exception*(__fastcall* s_construct)(Exception*);
Exception* __fastcall ConstructVerify(Exception* self) {
    Exception expected;
    std::memset(&expected, 0xCD, sizeof expected);
    Exception* expectedResult = s_construct(&expected);
    Exception* result = self->Construct();
    Check("exception::exception", true,
          std::memcmp(&expected, self, sizeof expected) == 0 && expectedResult == &expected && result == self,
          "%p %p %d / %p %p %d", expected.vtable, expected.text, expected.ownsText, self->vtable, self->text,
          self->ownsText);
    return result;
}
EI_VERIFY(0x006E7CD5, ConstructVerify, 6, s_construct);

// 006E7D3F: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC]; MOV ESI,ECX (8 bytes).
Exception*(__fastcall* s_copyConstruct)(Exception*, void*, const Exception*);
Exception* __fastcall CopyConstructVerify(Exception* self, void*, const Exception* source) {
    Exception expected;
    s_copyConstruct(&expected, nullptr, source);
    Exception* result = self->CopyConstruct(*source);
    bool same = result == self && expected.vtable == self->vtable && expected.ownsText == self->ownsText;
    if (self->ownsText) {
        same = same && self->text != source->text && expected.text && self->text &&
               std::strcmp(expected.text, self->text) == 0;
        gamecrt::operator_delete(const_cast<char*>(expected.text));
    } else {
        same = same && expected.text == self->text;
    }
    Check("exception copy", true, same, "owned %d / %d, text %p / %p", expected.ownsText, self->ownsText, expected.text,
          self->text);
    return result;
}
EI_VERIFY(0x006E7D3F, CopyConstructVerify, 8, s_copyConstruct);

// What a copy of `source` holds (ours afterwards), checked after operator= and the copy constructor.
bool HoldsCopy(const Exception& copy, const Exception& source) {
    if (copy.vtable != ExceptionVtable() || copy.ownsText != source.ownsText) return false;
    if (!source.ownsText) return copy.text == source.text;
    return copy.text && copy.text != source.text && std::strcmp(copy.text, source.text) == 0;
}

Exception*(__fastcall* s_unusedAssign)(Exception*, void*, const Exception*);
Exception* __fastcall AssignVerify(Exception* self, void*, const Exception* source) {
    Exception* result = self->Assign(*source);
    Check("exception::operator=", true, result == self && (self == source || HoldsCopy(*self, *source)),
          "owned %d, text %p from %p", self->ownsText, self->text, source->text);
    return result;
}
EI_VERIFY(0x006E7D89, AssignVerify, 0, s_unusedAssign);

void(__fastcall* s_unusedDestruct)(Exception*);
void __fastcall DestructVerify(Exception* self) {
    self->Destruct();
    Check("exception::~exception", ExceptionVtable(), self->vtable, "vtable %p", self->vtable);
}
EI_VERIFY(0x006E7DA8, DestructVerify, 0, s_unusedDestruct);

Exception*(__fastcall* s_unusedDeletingDestruct)(Exception*, void*, unsigned);
Exception* __fastcall DeletingDestructVerify(Exception* self, void*, unsigned flags) {
    Exception* result = self->DeletingDestruct(flags);
    Check("exception deleting destructor", static_cast<void*>(self), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x006E7CE6, DeletingDestructVerify, 0, s_unusedDeletingDestruct);

// 006E7DBE: MOV EAX,[ECX+4]; TEST EAX,EAX (5 bytes).
const char*(__fastcall* s_what)(const Exception*);
const char* __fastcall WhatVerify(const Exception* self) {
    const char* expected = s_what(self);
    const char* actual = self->What();
    Check("exception::what", expected, actual, "%p / %p", expected, actual);
    return actual;
}
EI_VERIFY(0x006E7DBE, WhatVerify, 5, s_what);

char* OwnedText(const char* text) {
    const std::size_t size = std::strlen(text) + 1;
    char* copy = static_cast<char*>(gamecrt::operator_new(size));
    std::memcpy(copy, text, size);
    return copy;
}

void ExceptionStartupCheck() {
    Exception plain;
    ConstructAt(&plain);
    WhatAt(&plain);
    Exception shared;
    ConstructAt(&shared);
    shared.text = "shared text";
    Exception owned;
    ConstructAt(&owned);
    owned.text = OwnedText("owned text");
    owned.ownsText = 1;
    Exception copies[3];
    CopyConstructAt(&copies[0], nullptr, &shared);
    CopyConstructAt(&copies[1], nullptr, &owned);
    CopyConstructAt(&copies[2], nullptr, &plain);
    for (const Exception& copy : copies) WhatAt(&copy);
    AssignAt(&copies[0], nullptr, &owned);     // shared to owned
    AssignAt(&copies[1], nullptr, &shared);    // owned to shared (the copy freed)
    AssignAt(&copies[1], nullptr, &copies[1]); // itself
    for (Exception& copy : copies) DestructAt(&copy);
    auto* heap = static_cast<Exception*>(gamecrt::operator_new(sizeof(Exception)));
    CopyConstructAt(heap, nullptr, &owned);
    DeletingDestructAt(heap, nullptr, 1);
    DestructAt(&owned);
    DestructAt(&shared);
    DestructAt(&plain);
}
EI_STARTUP_CHECK(ExceptionStartupCheck);

} // namespace
