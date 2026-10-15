// Checks for MFC's CString and CFixedAlloc (src/lib/mfc_string.cpp, ticket S5). Strings are plain
// data: a function that reads or rewrites one runs as the original on a private copy (its own
// buffer) and as ours on the caller's string, and the texts are compared; a function that builds a
// new string (constructors, Mid, operator+) runs the original into scratch storage. The allocator,
// the reference counting and the functions whose first five bytes hold a call run ours only, with
// checks of the state they leave. Every string the game touches passes through these.
#include "verify.h"

#include "../../src/lib/mfc_string.h"

#include <cstring>
#include <new>

using mfc::impl::CFixedAlloc;
using mfc::impl::CString;
using mfc::impl::CStringData;

namespace {

using ei::verify::Check;

// The text and its length match (the bytes up to and with the terminator).
bool Same(const CString& expected, const CString& actual) {
    int length = expected.GetLength();
    return length == actual.GetLength() &&
           std::memcmp(static_cast<const char*>(expected), static_cast<const char*>(actual),
                       static_cast<std::size_t>(length) + 1) == 0;
}

// A private copy with its own buffer (refs 1), for the original to run on.
CString Twin(const CString& s) { return CString(static_cast<const char*>(s), s.GetLength()); }

// Storage for a string the original constructs; Get() is valid once it has.
struct Slot {
    alignas(CString) unsigned char raw[sizeof(CString)];
    CString* Get() { return reinterpret_cast<CString*>(raw); }
};

const char* Text(const CString& s) { return static_cast<const char*>(s); }

// ---- Construction and assignment ------------------------------------------------------------------

using CopyFn = CString*(__fastcall*)(CString*, void*, const CString&);
CopyFn s_copy;
CString* __fastcall CopyConstructVerify(CString* self, void*, const CString& source) {
    Slot slot;
    CString* expected = s_copy(slot.Get(), nullptr, source);
    CString* result = ::new (self) CString(source);
    Check("CString::CString(const CString&)", true, Same(*expected, *result),
          "\"%.60s\" (original \"%.60s\")", Text(*result), Text(*expected));
    expected->~CString();
    return result;
}

using TextFn = CString*(__fastcall*)(CString*, void*, const char*);
TextFn s_text;
CString* __fastcall TextConstructVerify(CString* self, void*, const char* text) {
    Slot slot;
    CString* expected = s_text(slot.Get(), nullptr, text);
    CString* result = ::new (self) CString(text);
    Check("CString::CString(const char*)", true, Same(*expected, *result), "\"%.60s\" (original \"%.60s\")",
          Text(*result), Text(*expected));
    expected->~CString();
    return result;
}

using CountFn = CString*(__fastcall*)(CString*, void*, const char*, int);
CountFn s_count;
CString* __fastcall CountConstructVerify(CString* self, void*, const char* text, int length) {
    Slot slot;
    CString* expected = s_count(slot.Get(), nullptr, text, length);
    CString* result = ::new (self) CString(text, length);
    Check("CString::CString(const char*, int)", true, Same(*expected, *result), "%d: \"%.60s\"", length,
          Text(*result));
    expected->~CString();
    return result;
}

// Ours only: the string is alive (a count of 0 would be a second release).
void* s_unused;
void __fastcall DestructVerify(CString* self) {
    long refs = self->GetData()->refs;
    self->~CString();
    Check("CString::~CString", true, refs != 0, "refs %ld", refs);
}

// The assignments and appends: the original on a twin of the target. The argument may be the
// target itself or point into it; the original runs first, while the target is unchanged.
template <typename Arg>
using EditFn = const CString*(__fastcall*)(CString*, void*, Arg);

template <typename Arg, typename Edit>
const CString* CompareEdit(const char* name, EditFn<Arg> original, CString* self, Arg arg, Edit edit) {
    CString twin = Twin(*self);
    original(&twin, nullptr, arg);
    edit(*self, arg);
    Check(name, true, Same(twin, *self), "\"%.60s\" (original \"%.60s\")", Text(*self), Text(twin));
    return self;
}

EditFn<const CString&> s_assignString;
const CString* __fastcall AssignStringVerify(CString* self, void*, const CString& source) {
    return CompareEdit<const CString&>("CString::operator=(const CString&)", s_assignString, self, source,
                                       [](CString& s, const CString& a) { s = a; });
}

EditFn<const char*> s_assignText;
const CString* __fastcall AssignTextVerify(CString* self, void*, const char* text) {
    return CompareEdit<const char*>("CString::operator=(const char*)", s_assignText, self, text,
                                    [](CString& s, const char* a) { s = a; });
}

EditFn<char> s_assignChar;
const CString* __fastcall AssignCharVerify(CString* self, void*, char c) {
    return CompareEdit<char>("CString::operator=(char)", s_assignChar, self, c, [](CString& s, char a) { s = a; });
}

EditFn<const CString&> s_appendString;
const CString* __fastcall AppendStringVerify(CString* self, void*, const CString& text) {
    return CompareEdit<const CString&>("CString::operator+=(const CString&)", s_appendString, self, text,
                                       [](CString& s, const CString& a) { s += a; });
}

EditFn<const char*> s_appendText;
const CString* __fastcall AppendTextVerify(CString* self, void*, const char* text) {
    return CompareEdit<const char*>("CString::operator+=(const char*)", s_appendText, self, text,
                                    [](CString& s, const char* a) { s += a; });
}

EditFn<char> s_appendChar;
const CString* __fastcall AppendCharVerify(CString* self, void*, char c) {
    return CompareEdit<char>("CString::operator+=(char)", s_appendChar, self, c, [](CString& s, char a) { s += a; });
}

// operator+: the original into scratch storage.
template <typename A, typename B>
using PlusFn = CString*(__stdcall*)(CString*, A, B);

template <typename A, typename B>
CString* ComparePlus(const char* name, PlusFn<A, B> original, CString* result, A a, B b) {
    Slot slot;
    CString* expected = original(slot.Get(), a, b);
    ::new (result) CString(a + b);
    Check(name, true, Same(*expected, *result), "\"%.60s\" (original \"%.60s\")", Text(*result), Text(*expected));
    expected->~CString();
    return result;
}

PlusFn<const CString&, const CString&> s_plusStrings;
CString* __stdcall PlusStringsVerify(CString* result, const CString& a, const CString& b) {
    return ComparePlus<const CString&, const CString&>("operator+(CString, CString)", s_plusStrings, result, a, b);
}
PlusFn<const CString&, const char*> s_plusStringText;
CString* __stdcall PlusStringTextVerify(CString* result, const CString& a, const char* b) {
    return ComparePlus<const CString&, const char*>("operator+(CString, const char*)", s_plusStringText, result, a, b);
}
PlusFn<const char*, const CString&> s_plusTextString;
CString* __stdcall PlusTextStringVerify(CString* result, const char* a, const CString& b) {
    return ComparePlus<const char*, const CString&>("operator+(const char*, CString)", s_plusTextString, result, a, b);
}
PlusFn<const CString&, char> s_plusStringChar;
CString* __stdcall PlusStringCharVerify(CString* result, const CString& a, char b) {
    return ComparePlus<const CString&, char>("operator+(CString, char)", s_plusStringChar, result, a, b);
}
PlusFn<char, const CString&> s_plusCharString;
CString* __stdcall PlusCharStringVerify(CString* result, char a, const CString& b) {
    return ComparePlus<char, const CString&>("operator+(char, CString)", s_plusCharString, result, a, b);
}

// ---- Buffers ------------------------------------------------------------------------------------

using VoidFn = void(__fastcall*)(CString*);
VoidFn s_empty;
void __fastcall EmptyVerify(CString* self) {
    CString twin = Twin(*self);
    s_empty(&twin);
    self->Empty();
    Check("CString::Empty", true, Same(twin, *self) && self->GetLength() == 0, "\"%.60s\"", Text(*self));
}

using BufferFn = char*(__fastcall*)(CString*, void*, int);
BufferFn s_getBuffer;
char* __fastcall GetBufferVerify(CString* self, void*, int minLength) {
    CString twin = Twin(*self);
    char* expected = s_getBuffer(&twin, nullptr, minLength);
    char* result = self->GetBuffer(minLength);
    bool ok = Same(twin, *self) && expected == Text(twin) && result == Text(*self) &&
              self->GetData()->allocLength >= minLength && self->GetData()->refs <= 1;
    Check("CString::GetBuffer", true, ok, "%d: \"%.60s\" (original \"%.60s\")", minLength, Text(*self), Text(twin));
    return result;
}

// The characters past the old text are whatever the buffer held: only the old text is compared.
BufferFn s_setLength;
char* __fastcall GetBufferSetLengthVerify(CString* self, void*, int length) {
    int kept = self->GetLength() < length ? self->GetLength() : length;
    CString twin = Twin(*self);
    char* expected = s_setLength(&twin, nullptr, length);
    char* result = self->GetBufferSetLength(length);
    bool ok = twin.GetLength() == length && self->GetLength() == length && expected == Text(twin) &&
              result == Text(*self) && result[length] == '\0' &&
              std::memcmp(expected, result, static_cast<std::size_t>(kept)) == 0;
    Check("CString::GetBufferSetLength", true, ok, "%d", length);
    return result;
}

// Ours only (a call within the first five bytes): the length is the one given, or up to the
// terminator for -1.
void __fastcall ReleaseBufferVerify(CString* self, void*, int newLength) {
    self->ReleaseBuffer(newLength);
    int length = self->GetLength();
    bool ok = (newLength == -1 ? length == static_cast<int>(std::strlen(Text(*self))) : length == newLength) &&
              Text(*self)[length] == '\0' && (self->GetData()->refs <= 1);
    Check("CString::ReleaseBuffer", true, ok, "%d -> %d", newLength, length);
}

using LockFn = char*(__fastcall*)(CString*);
LockFn s_lock;
char* __fastcall LockBufferVerify(CString* self) {
    CString twin = Twin(*self);
    char* expected = s_lock(&twin);
    char* result = self->LockBuffer();
    bool ok = Same(twin, *self) && expected == Text(twin) && result == Text(*self) &&
              twin.GetData()->refs == self->GetData()->refs;
    twin.UnlockBuffer();
    Check("CString::LockBuffer", true, ok, "\"%.60s\"", Text(*self));
    return result;
}

// The twin carries the same count as the string (a locked string's -1 included).
VoidFn s_unlock;
void __fastcall UnlockBufferVerify(CString* self) {
    CString twin = Twin(*self);
    bool twinIsNil = twin.GetData() == mfc::impl::DataNil();
    if (!twinIsNil) twin.GetData()->refs = self->GetData()->refs;
    s_unlock(&twin);
    self->UnlockBuffer();
    bool ok = twinIsNil ? (self->GetData() == mfc::impl::DataNil() || self->GetData()->refs == 1)
                        : twin.GetData()->refs == self->GetData()->refs;
    if (!twinIsNil) twin.GetData()->refs = 1;
    Check("CString::UnlockBuffer", true, ok, "refs %ld", self->GetData()->refs);
}

// ---- Searching and extracting ---------------------------------------------------------------------

template <typename Fn, typename... Args>
int CompareFind(const char* name, Fn original, const CString* self, int result, Args... args) {
    int expected = original(self, nullptr, args...);
    Check(name, expected, result, "\"%.60s\": %d (original %d)", Text(*self), result, expected);
    return expected;
}

using FindCharFn = int(__fastcall*)(const CString*, void*, char);
FindCharFn s_findChar;
int __fastcall FindCharVerify(const CString* self, void*, char c) {
    return CompareFind("CString::Find(char)", s_findChar, self, self->Find(c), c);
}
using FindCharFromFn = int(__fastcall*)(const CString*, void*, char, int);
FindCharFromFn s_findCharFrom;
int __fastcall FindCharFromVerify(const CString* self, void*, char c, int start) {
    return CompareFind("CString::Find(char, int)", s_findCharFrom, self, self->Find(c, start), c, start);
}
using FindTextFn = int(__fastcall*)(const CString*, void*, const char*);
FindTextFn s_findText;
int __fastcall FindTextVerify(const CString* self, void*, const char* sub) {
    return CompareFind("CString::Find(const char*)", s_findText, self, self->Find(sub), sub);
}
using FindTextFromFn = int(__fastcall*)(const CString*, void*, const char*, int);
FindTextFromFn s_findTextFrom;
int __fastcall FindTextFromVerify(const CString* self, void*, const char* sub, int start) {
    return CompareFind("CString::Find(const char*, int)", s_findTextFrom, self, self->Find(sub, start), sub, start);
}
FindCharFn s_reverseFind;
int __fastcall ReverseFindVerify(const CString* self, void*, char c) {
    return CompareFind("CString::ReverseFind", s_reverseFind, self, self->ReverseFind(c), c);
}
FindTextFn s_findOneOf;
int __fastcall FindOneOfVerify(const CString* self, void*, const char* set) {
    return CompareFind("CString::FindOneOf", s_findOneOf, self, self->FindOneOf(set), set);
}

// Mid / Left / Right: the original into scratch storage.
template <typename Fn, typename Part, typename... Args>
CString* CompareExtract(const char* name, Fn original, const CString* self, CString* result, Part part, Args... args) {
    Slot slot;
    CString* expected = original(self, nullptr, slot.Get(), args...);
    ::new (result) CString(part());
    Check(name, true, Same(*expected, *result), "\"%.60s\" -> \"%.60s\" (original \"%.60s\")", Text(*self),
          Text(*result), Text(*expected));
    expected->~CString();
    return result;
}

using MidFromFn = CString*(__fastcall*)(const CString*, void*, CString*, int);
MidFromFn s_midFrom;
CString* __fastcall MidFromVerify(const CString* self, void*, CString* result, int first) {
    return CompareExtract("CString::Mid(int)", s_midFrom, self, result, [&] { return self->Mid(first); }, first);
}
using MidCountFn = CString*(__fastcall*)(const CString*, void*, CString*, int, int);
MidCountFn s_midCount;
CString* __fastcall MidCountVerify(const CString* self, void*, CString* result, int first, int count) {
    return CompareExtract("CString::Mid(int, int)", s_midCount, self, result,
                          [&] { return self->Mid(first, count); }, first, count);
}
MidFromFn s_left;
CString* __fastcall LeftVerify(const CString* self, void*, CString* result, int count) {
    return CompareExtract("CString::Left", s_left, self, result, [&] { return self->Left(count); }, count);
}
MidFromFn s_right;
CString* __fastcall RightVerify(const CString* self, void*, CString* result, int count) {
    return CompareExtract("CString::Right", s_right, self, result, [&] { return self->Right(count); }, count);
}

// ---- Editing ------------------------------------------------------------------------------------

using MbsLwrFn = unsigned char*(__cdecl*)(unsigned char*);
// Ours only (a call within the first five bytes): against the CRT's _mbslwr on a twin.
void __fastcall MakeLowerVerify(CString* self) {
    CString twin = Twin(*self);
    if (twin.GetLength()) reinterpret_cast<MbsLwrFn>(0x006EC8A8)(reinterpret_cast<unsigned char*>(twin.GetBuffer(0)));
    self->MakeLower();
    Check("CString::MakeLower", true, Same(twin, *self), "\"%.60s\" (expected \"%.60s\")", Text(*self), Text(twin));
}

using RemoveFn = int(__fastcall*)(CString*, void*, char);
RemoveFn s_remove;
int __fastcall RemoveVerify(CString* self, void*, char c) {
    CString twin = Twin(*self);
    int expected = s_remove(&twin, nullptr, c);
    int result = self->Remove(c);
    Check("CString::Remove", true, Same(twin, *self) && expected == result, "\"%.60s\" %d (original %d)", Text(*self),
          result, expected);
    return result;
}

VoidFn s_trimRight;
void __fastcall TrimRightVerify(CString* self) {
    CString twin = Twin(*self);
    s_trimRight(&twin);
    self->TrimRight();
    Check("CString::TrimRight()", true, Same(twin, *self), "\"%.60s\" (original \"%.60s\")", Text(*self), Text(twin));
}

using TrimSetFn = void(__fastcall*)(CString*, void*, const char*);
TrimSetFn s_trimRightSet;
void __fastcall TrimRightSetVerify(CString* self, void*, const char* targets) {
    CString twin = Twin(*self);
    s_trimRightSet(&twin, nullptr, targets);
    self->TrimRight(targets);
    Check("CString::TrimRight(const char*)", true, Same(twin, *self), "\"%.60s\" (original \"%.60s\")", Text(*self),
          Text(twin));
}

// Ours only (a call within the first five bytes): the result is a prefix (TrimRight) or a suffix
// (TrimLeft) of the text and no longer starts or ends with the target.
void __fastcall TrimRightCharVerify(CString* self, void*, char target) {
    CString before = Twin(*self);
    self->TrimRight(target);
    int length = self->GetLength();
    bool ok = length <= before.GetLength() && std::memcmp(Text(before), Text(*self), static_cast<std::size_t>(length)) == 0 &&
              Text(*self)[length] == '\0' && (length == 0 || Text(*self)[length - 1] != target);
    Check("CString::TrimRight(char)", true, ok, "\"%.60s\" -> \"%.60s\"", Text(before), Text(*self));
}

void __fastcall TrimLeftCharVerify(CString* self, void*, char target) {
    CString before = Twin(*self);
    self->TrimLeft(target);
    int length = self->GetLength();
    int cut = before.GetLength() - length;
    bool ok = cut >= 0 && std::memcmp(Text(before) + cut, Text(*self), static_cast<std::size_t>(length) + 1) == 0 &&
              (length == 0 || Text(*self)[0] != target);
    Check("CString::TrimLeft(char)", true, ok, "\"%.60s\" -> \"%.60s\"", Text(before), Text(*self));
}

void __fastcall TrimLeftVerify(CString* self) {
    CString before = Twin(*self);
    self->TrimLeft();
    int length = self->GetLength();
    int cut = before.GetLength() - length;
    bool ok = cut >= 0 && std::memcmp(Text(before) + cut, Text(*self), static_cast<std::size_t>(length) + 1) == 0 &&
              (length == 0 || Text(*self)[0] != ' ');
    Check("CString::TrimLeft()", true, ok, "\"%.60s\" -> \"%.60s\"", Text(before), Text(*self));
}

// ---- Formatting ---------------------------------------------------------------------------------

using FormatVFn = void(__fastcall*)(CString*, void*, const char*, va_list);
FormatVFn s_formatV;
void __fastcall FormatVVerify(CString* self, void*, const char* format, va_list arguments) {
    CString twin = Twin(*self);
    s_formatV(&twin, nullptr, format, arguments);
    self->FormatV(format, arguments);
    bool ok = Same(twin, *self) && twin.GetData()->allocLength == self->GetData()->allocLength;
    Check("CString::FormatV", true, ok, "\"%.60s\" (original \"%.60s\", buffers %d / %d)", Text(*self), Text(twin),
          self->GetData()->allocLength, twin.GetData()->allocLength);
}

// Format runs ours; the original's FormatV (when its wrapper is in) prints the same arguments again.
void __cdecl FormatVerify(CString* self, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (s_formatV) {
        CString twin = Twin(*self);
        s_formatV(&twin, nullptr, format, arguments);
        self->FormatV(format, arguments);
        Check("CString::Format", true, Same(twin, *self), "\"%.60s\" (original \"%.60s\")", Text(*self), Text(twin));
    } else {
        self->FormatV(format, arguments);
    }
    va_end(arguments);
}

// ---- Elements -------------------------------------------------------------------------------------

void __stdcall ConstructStringsVerify(CString* elements, int count) {
    mfc::impl::ConstructStrings(elements, count);
    bool ok = true;
    for (int i = 0; i < count; ++i) ok = ok && Text(elements[i]) == mfc::impl::PchNil();
    Check("ConstructElements<CString>", true, ok, "%d", count);
}

void __stdcall DestructStringsVerify(CString* elements, int count) {
    bool ok = true;
    for (int i = 0; i < count; ++i) ok = ok && elements[i].GetData()->refs != 0;
    mfc::impl::DestructStrings(elements, count);
    Check("DestructElements<CString>", true, ok, "%d", count);
}

// ---- The library's helpers ----------------------------------------------------------------------

// Ours only: the new buffer is the length asked for, unshared and terminated.
void __fastcall AllocBufferVerify(CString* self, void*, int length) {
    self->AllocBuffer(length);
    CStringData* data = self->GetData();
    bool ok = length == 0 ? data == mfc::impl::DataNil()
                          : data->refs == 1 && data->dataLength == length && data->allocLength >= length &&
                                Text(*self)[length] == '\0';
    Check("CString::AllocBuffer", true, ok, "%d", length);
}

void __fastcall FreeDataVerify(CStringData* data) {
    bool ok = data != mfc::impl::DataNil() && data->refs <= 0;
    CString::FreeData(data);
    Check("CString::FreeData", true, ok, "refs %ld", data->refs);
}

void __fastcall ReleaseVerify(CString* self) {
    self->Release();
    Check("CString::Release()", true, Text(*self) == mfc::impl::PchNil(), "");
}

void __stdcall ReleaseDataVerify(CStringData* data) {
    bool ok = data == mfc::impl::DataNil() || data->refs != 0;
    CString::Release(data);
    Check("CString::Release(CStringData*)", true, ok, "");
}

VoidFn s_copyBeforeWrite;
void __fastcall CopyBeforeWriteVerify(CString* self) {
    CString twin = Twin(*self);
    s_copyBeforeWrite(&twin);
    self->CopyBeforeWrite();
    Check("CString::CopyBeforeWrite", true, Same(twin, *self) && self->GetData()->refs <= 1, "\"%.60s\"", Text(*self));
}

// Ours only: afterwards the buffer is unshared and holds `length` characters.
void __fastcall AllocBeforeWriteVerify(CString* self, void*, int length) {
    self->AllocBeforeWrite(length);
    CStringData* data = self->GetData();
    bool ok = data == mfc::impl::DataNil() ? length == 0 : data->refs <= 1 && data->allocLength >= length;
    Check("CString::AllocBeforeWrite", true, ok, "%d", length);
}

// AllocCopy writes `dest` without releasing it: the original writes a scratch string.
using AllocCopyFn = void(__fastcall*)(const CString*, void*, CString&, int, int, int);
AllocCopyFn s_allocCopy;
void __fastcall AllocCopyVerify(const CString* self, void*, CString& dest, int copyLength, int copyIndex, int extraLength) {
    Slot slot;
    s_allocCopy(self, nullptr, *slot.Get(), copyLength, copyIndex, extraLength);
    self->AllocCopy(dest, copyLength, copyIndex, extraLength);
    // The extra characters are not written: compare the copied ones and the length.
    CString& expected = *slot.Get();
    bool ok = expected.GetLength() == dest.GetLength() &&
              std::memcmp(Text(expected), Text(dest), static_cast<std::size_t>(copyLength)) == 0;
    Check("CString::AllocCopy", true, ok, "%d at %d + %d", copyLength, copyIndex, extraLength);
    expected.~CString();
}

using AssignCopyFn = void(__fastcall*)(CString*, void*, int, const char*);
AssignCopyFn s_assignCopy;
void __fastcall AssignCopyVerify(CString* self, void*, int length, const char* text) {
    CString twin = Twin(*self);
    s_assignCopy(&twin, nullptr, length, text);
    self->AssignCopy(length, text);
    Check("CString::AssignCopy", true, Same(twin, *self), "%d: \"%.60s\"", length, Text(*self));
}

// ConcatCopy writes over the string without releasing it, and leaves it alone for an empty
// result: the original works on a scratch string that starts with the same pointer.
using ConcatCopyFn = void(__fastcall*)(CString*, void*, int, const char*, int, const char*);
ConcatCopyFn s_concatCopy;
void __fastcall ConcatCopyVerify(CString* self, void*, int length1, const char* text1, int length2, const char* text2) {
    Slot slot;
    std::memcpy(slot.raw, self, sizeof(CString));
    s_concatCopy(slot.Get(), nullptr, length1, text1, length2, text2);
    self->ConcatCopy(length1, text1, length2, text2);
    CString& expected = *slot.Get();
    Check("CString::ConcatCopy", true, Same(expected, *self), "%d + %d: \"%.60s\"", length1, length2, Text(*self));
    if (length1 + length2 != 0) expected.~CString();
}

using ConcatInPlaceFn = void(__fastcall*)(CString*, void*, int, const char*);
ConcatInPlaceFn s_concatInPlace;
void __fastcall ConcatInPlaceVerify(CString* self, void*, int length, const char* text) {
    CString twin = Twin(*self);
    s_concatInPlace(&twin, nullptr, length, text);
    self->ConcatInPlace(length, text);
    Check("CString::ConcatInPlace", true, Same(twin, *self), "%d: \"%.60s\"", length, Text(*self));
}

// ---- CFixedAlloc ----------------------------------------------------------------------------------

// Ours only: blocks are distinct dword-aligned pointers.
void* __fastcall FixedAllocVerify(CFixedAlloc* self) {
    void* block = self->Alloc();
    Check("CFixedAlloc::Alloc", true, block != nullptr && (reinterpret_cast<std::uintptr_t>(block) & 3) == 0,
          "%p", block);
    return block;
}

void __fastcall FixedFreeVerify(CFixedAlloc* self, void*, void* block) {
    self->Free(block);
    Check("CFixedAlloc::Free", true, true, "");
}

void __fastcall FixedFreeAllVerify(CFixedAlloc* self) {
    self->FreeAll();
    Check("CFixedAlloc::FreeAll", true, true, "");
}

void __fastcall FixedDestructVerify(CFixedAlloc* self) {
    self->Destruct();
    Check("CFixedAlloc::~CFixedAlloc", true, true, "");
}

CFixedAlloc* __fastcall FixedConstructVerify(CFixedAlloc* self, void*, unsigned allocSize, unsigned blockSize) {
    CFixedAlloc* result = self->Construct(allocSize, blockSize);
    Check("CFixedAlloc::CFixedAlloc", true, result == self && self->AllocSize() == allocSize, "%u", allocSize);
    return result;
}

// The static initialisers built the four string allocators before the hooks: ours builds the same
// sizes (header + characters + terminator rounded up to 4, 64 per chunk).
void StringAllocatorsCheck() {
    CFixedAlloc* live[] = {&mfc::impl::StringAlloc64(), &mfc::impl::StringAlloc128(), &mfc::impl::StringAlloc256(),
                           &mfc::impl::StringAlloc512()};
    const unsigned characters[] = {64, 128, 256, 512};
    for (int i = 0; i < 4; ++i) {
        alignas(CFixedAlloc) unsigned char raw[sizeof(CFixedAlloc)];
        CFixedAlloc* ours = reinterpret_cast<CFixedAlloc*>(raw)->Construct(
            (static_cast<unsigned>(sizeof(CStringData)) + characters[i] + 1 + 3) & ~3u, 64);
        Check("CFixedAlloc (string allocators)", true,
              ours->AllocSize() == live[i]->AllocSize() && ours->BlockSize() == live[i]->BlockSize(),
              "%u: %u / %u", characters[i], live[i]->AllocSize(), live[i]->BlockSize());
        ours->Destruct();
    }
}

} // namespace

EI_VERIFY(0x007006E5, CopyConstructVerify, 7, s_copy);
EI_VERIFY(0x007009DE, TextConstructVerify, 5, s_text);
EI_VERIFY(0x006FD4DE, CountConstructVerify, 5, s_count);
EI_VERIFY(0x00700970, DestructVerify, 0, s_unused);
EI_VERIFY(0x00700AA9, AssignStringVerify, 6, s_assignString);
EI_VERIFY(0x00700AF9, AssignTextVerify, 6, s_assignText);
EI_VERIFY(0x006FD559, AssignCharVerify, 5, s_assignChar);
EI_VERIFY(0x00700D88, AppendStringVerify, 5, s_appendString);
EI_VERIFY(0x00700D4C, AppendTextVerify, 6, s_appendText);
EI_VERIFY(0x00700D73, AppendCharVerify, 5, s_appendChar);
EI_VERIFY(0x00700B9F, PlusStringsVerify, 5, s_plusStrings);
EI_VERIFY(0x00700C05, PlusStringTextVerify, 5, s_plusStringText);
EI_VERIFY(0x00700C79, PlusTextStringVerify, 5, s_plusTextString);
EI_VERIFY(0x006FD56E, PlusStringCharVerify, 5, s_plusStringChar);
EI_VERIFY(0x006FD5D0, PlusCharStringVerify, 5, s_plusCharString);
EI_VERIFY(0x007008FB, EmptyVerify, 6, s_empty);
EI_VERIFY(0x00700DA0, GetBufferVerify, 8, s_getBuffer);
EI_VERIFY(0x00700DEF, ReleaseBufferVerify, 0, s_unused);
EI_VERIFY(0x00700E17, GetBufferSetLengthVerify, 6, s_setLength);
EI_VERIFY(0x00700E6A, LockBufferVerify, 5, s_lock);
EI_VERIFY(0x00700E7C, UnlockBufferVerify, 5, s_unlock);
EI_VERIFY(0x00700E91, FindCharVerify, 6, s_findChar);
EI_VERIFY(0x00700E9F, FindCharFromVerify, 7, s_findCharFrom);
EI_VERIFY(0x006FDBD9, FindTextVerify, 6, s_findText);
EI_VERIFY(0x006FDBE7, FindTextFromVerify, 7, s_findTextFrom);
EI_VERIFY(0x006FDBB7, ReverseFindVerify, 8, s_reverseFind);
EI_VERIFY(0x00700ECC, FindOneOfVerify, 7, s_findOneOf);
EI_VERIFY(0x006FD9B4, MidFromVerify, 6, s_midFrom);
EI_VERIFY(0x006FD9D7, MidCountVerify, 5, s_midCount);
EI_VERIFY(0x006FDAE9, LeftVerify, 5, s_left);
EI_VERIFY(0x006FDA6D, RightVerify, 5, s_right);
EI_VERIFY(0x00700EFE, MakeLowerVerify, 0, s_unused);
EI_VERIFY(0x006FD963, RemoveVerify, 8, s_remove);
EI_VERIFY(0x006FE11F, TrimRightVerify, 5, s_trimRight);
EI_VERIFY(0x006FE0DD, TrimRightCharVerify, 0, s_unused);
EI_VERIFY(0x006FE08A, TrimRightSetVerify, 5, s_trimRightSet);
EI_VERIFY(0x006FE216, TrimLeftVerify, 0, s_unused);
EI_VERIFY(0x006FE1D3, TrimLeftCharVerify, 0, s_unused);
EI_VERIFY(0x006FDC12, FormatVVerify, 6, s_formatV);
EI_VERIFY(0x006FDF5F, FormatVerify, 0, s_unused);
EI_VERIFY(0x006FE268, ConstructStringsVerify, 0, s_unused);
EI_VERIFY(0x006FE296, DestructStringsVerify, 0, s_unused);
EI_VERIFY(0x007007DD, AllocBufferVerify, 0, s_unused);
EI_VERIFY(0x0070085F, FreeDataVerify, 0, s_unused);
EI_VERIFY(0x007008A7, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x007008D8, ReleaseDataVerify, 0, s_unused);
EI_VERIFY(0x00700919, CopyBeforeWriteVerify, 6, s_copyBeforeWrite);
EI_VERIFY(0x00700947, AllocBeforeWriteVerify, 0, s_unused);
EI_VERIFY(0x0070099A, AllocCopyVerify, 6, s_allocCopy);
EI_VERIFY(0x00700A7C, AssignCopyVerify, 6, s_assignCopy);
EI_VERIFY(0x00700B61, ConcatCopyVerify, 5, s_concatCopy);
EI_VERIFY(0x00700CED, ConcatInPlaceVerify, 7, s_concatInPlace);
EI_VERIFY(0x006E6BCD, FixedConstructVerify, 0, s_unused);
EI_VERIFY(0x006E6BF5, FixedDestructVerify, 0, s_unused);
EI_VERIFY(0x006E6C09, FixedFreeAllVerify, 0, s_unused);
EI_VERIFY(0x006E6C31, FixedAllocVerify, 0, s_unused);
EI_VERIFY(0x006E6CC0, FixedFreeVerify, 0, s_unused);
EI_STARTUP_CHECK(StringAllocatorsCheck);
