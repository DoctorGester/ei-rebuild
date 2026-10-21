// MFC 4.2's CString and CFixedAlloc, registered at their library addresses (mfc_string.h).
#include "mfc_string.h"

#include "crt.h"
#include "ei/hook.h"
#include "gameapi.h"
#include "mfc_resource.h"
#include "game/originals_library.h"   // MbsInc
#include "game/game_data.h"

#include <cstring>
#include <new>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

using uchar = unsigned char;

// The game CRT's vsprintf. Its multibyte-aware string functions (libcrt::Mbschr and the others, crt.h)
// follow the CRT's code page.
__declspec(naked) int __cdecl VSPrintF(char*, const char*, va_list) EI_ORIGINAL(0x006ECC47)   // vsprintf

// The string helpers take the CRT's char arguments the way the library passes them.
const uchar* U(const char* p) { return reinterpret_cast<const uchar*>(p); }
uchar* U(char* p) { return reinterpret_cast<uchar*>(p); }
char* Inc(const char* p) { return const_cast<char*>(::MbsInc(p)); }
// _tcschr with a char the library sign-extends (movsx) before the call.
bool Contains(const char* set, char c) { return libcrt::Mbschr(U(set), static_cast<unsigned>(static_cast<int>(c))) != nullptr; }
bool IsSpace(char c) { return libcrt::Ismbcspace(static_cast<unsigned>(static_cast<int>(c))) != 0; }
bool IsDigit(char c) { return libcrt::Ismbcdigit(static_cast<unsigned>(static_cast<int>(c))) != 0; }

// The reference count updates and lstrlen go through the import table, as the library's do (ei_vectors
// has no game image: the API itself).
#ifdef EI_VECTORS_REPLAY
LONG Increment(long* refs) { return ::InterlockedIncrement(refs); }
LONG Decrement(long* refs) { return ::InterlockedDecrement(refs); }
int StrLen(const char* text) { return ::lstrlenA(text); }
#else
using InterlockedFn = LONG(WINAPI*)(LONG volatile*);
LONG Increment(long* refs) { return (ei::GameData<InterlockedFn>(0x0073B254))(refs); }
LONG Decrement(long* refs) { return (ei::GameData<InterlockedFn>(0x0073B258))(refs); }
int StrLen(const char* text) { return gameapi::lstrlenA(text); }
#endif

// SafeStrlen: lstrlen, 0 for null.
int SafeLength(const char* text) { return text ? StrLen(text) : 0; }

} // namespace

// ---- CFixedAlloc --------------------------------------------------------------------------------

CFixedAlloc* CFixedAlloc::Construct(unsigned allocSize, unsigned blockSize) {
    freeList_ = nullptr;
    blocks_ = nullptr;
    allocSize_ = allocSize;
    blockSize_ = blockSize;
    gameapi::InitializeCriticalSection(&lock_);
    return this;
}

void CFixedAlloc::Destruct() {
    FreeAll();
    gameapi::DeleteCriticalSection(&lock_);
}

void CFixedAlloc::FreeAll() {
    gameapi::EnterCriticalSection(&lock_);
    PlexFreeDataChain(blocks_);
    blocks_ = nullptr;
    freeList_ = nullptr;
    gameapi::LeaveCriticalSection(&lock_);
}

// A new chunk's blocks go on the free list last to first, so the first block is handed out first.
void* CFixedAlloc::Alloc() {
    gameapi::EnterCriticalSection(&lock_);
    if (!freeList_) {
        try {
            CPlex* chunk = PlexCreate(blocks_, blockSize_, allocSize_);
            auto* bytes = reinterpret_cast<std::uint8_t*>(chunk + 1) + allocSize_ * (blockSize_ - 1);
            for (int i = static_cast<int>(blockSize_) - 1; i >= 0; --i, bytes -= allocSize_) {
                Node* node = reinterpret_cast<Node*>(bytes);
                node->next = freeList_;
                freeList_ = node;
            }
        } catch (...) {
            gameapi::LeaveCriticalSection(&lock_);
            throw;
        }
    }
    Node* block = freeList_;
    freeList_ = block->next;
    gameapi::LeaveCriticalSection(&lock_);
    return block;
}

void CFixedAlloc::Free(void* block) {
    if (!block) return;
    gameapi::EnterCriticalSection(&lock_);
    Node* node = static_cast<Node*>(block);
    node->next = freeList_;
    freeList_ = node;
    gameapi::LeaveCriticalSection(&lock_);
}

// ---- CString: construction and buffers ----------------------------------------------------------

CString::CString(const CString& source) {
    if (source.GetData()->refs >= 0) {
        data_ = source.data_;
        Increment(&GetData()->refs);
    } else {
        Init();
        *this = source.data_;
    }
}

CString::CString(const char* text) {
    Init();
    if (text && HIWORD(reinterpret_cast<std::uintptr_t>(text)) == 0) {
        LoadResourceString(LOWORD(reinterpret_cast<std::uintptr_t>(text)));
    } else if (text) {
        int length = StrLen(text);
        if (length != 0) {
            AllocBuffer(length);
            std::memcpy(data_, text, static_cast<std::size_t>(length));
        }
    }
}

CString::CString(const char* text, int length) {
    Init();
    if (length != 0) {
        AllocBuffer(length);
        std::memcpy(data_, text, static_cast<std::size_t>(length));
    }
}

CString::~CString() {
    if (GetData() != DataNil() && Decrement(&GetData()->refs) <= 0) FreeData(GetData());
}

// Up to 512 characters from the fixed allocators (the block's size becomes allocLength), longer ones
// from operator new with exactly the room asked for.
void CString::AllocBuffer(int length) {
    if (length == 0) {
        Init();
        return;
    }
    CStringData* data;
#ifdef EI_VECTORS_REPLAY
    // ei_vectors has no fixed allocators: exactly the room asked for, as its own CString allocates
    // (tests/vectors/replay_mfc.cpp), so either may free the other's strings.
    data = static_cast<CStringData*>(gamecrt::operator_new(sizeof(CStringData) + static_cast<std::size_t>(length) + 1));
    data->allocLength = length;
#else
    if (length <= 64) {
        data = static_cast<CStringData*>(StringAlloc64().Alloc());
        data->allocLength = 64;
    } else if (length <= 128) {
        data = static_cast<CStringData*>(StringAlloc128().Alloc());
        data->allocLength = 128;
    } else if (length <= 256) {
        data = static_cast<CStringData*>(StringAlloc256().Alloc());
        data->allocLength = 256;
    } else if (length <= 512) {
        data = static_cast<CStringData*>(StringAlloc512().Alloc());
        data->allocLength = 512;
    } else {
        data = static_cast<CStringData*>(gamecrt::operator_new(sizeof(CStringData) + static_cast<std::size_t>(length) + 1));
        data->allocLength = length;
    }
#endif
    data->refs = 1;
    data->data()[length] = '\0';
    data->dataLength = length;
    data_ = data->data();
}

void __fastcall CString::FreeData(CStringData* data) {
#ifdef EI_VECTORS_REPLAY
    gamecrt::operator_delete(data);   // AllocBuffer's
#else
    switch (data->allocLength) {
    case 64: StringAlloc64().Free(data); break;
    case 128: StringAlloc128().Free(data); break;
    case 256: StringAlloc256().Free(data); break;
    case 512: StringAlloc512().Free(data); break;
    default: gamecrt::operator_delete(data); break;
    }
#endif
}

void CString::Release() {
    if (GetData() == DataNil()) return;
    if (Decrement(&GetData()->refs) <= 0) FreeData(GetData());
    Init();
}

void __stdcall CString::Release(CStringData* data) {
    if (data != DataNil() && Decrement(&data->refs) <= 0) FreeData(data);
}

void CString::Empty() {
    if (GetData()->dataLength == 0) return;
    if (GetData()->refs >= 0) Release();
    else *this = "";
}

// A fixed buffer first; a string that may not have fit (within two bytes of the end) is loaded again
// into the string's own buffer, 256 bytes larger each time, until it fits.
BOOL CString::LoadResourceString(unsigned id) {
    constexpr int kStep = 256;
    constexpr unsigned kFudge = 2;   // CHAR_FUDGE
    char fixed[kStep];
    int length = AfxLoadString(id, fixed, kStep);
    if (static_cast<unsigned>(kStep) - static_cast<unsigned>(length) > kFudge) {
        *this = fixed;
        return length > 0;
    }
    int size = kStep;
    do {
        size += kStep;
        length = AfxLoadString(id, GetBuffer(size - 1), static_cast<unsigned>(size));
    } while (size - length <= static_cast<int>(kFudge));
    ReleaseBuffer(-1);
    return length > 0;
}

void CString::CopyBeforeWrite() {
    if (GetData()->refs <= 1) return;
    CStringData* old = GetData();
    Release();
    AllocBuffer(old->dataLength);
    std::memcpy(data_, old->data(), static_cast<std::size_t>(old->dataLength) + 1);
}

void CString::AllocBeforeWrite(int length) {
    if (GetData()->refs > 1 || length > GetData()->allocLength) {
        Release();
        AllocBuffer(length);
    }
}

void CString::AllocCopy(CString& dest, int copyLength, int copyIndex, int extraLength) const {
    int length = copyLength + extraLength;
    if (length == 0) {
        dest.Init();
    } else {
        dest.AllocBuffer(length);
        std::memcpy(dest.data_, data_ + copyIndex, static_cast<std::size_t>(copyLength));
    }
}

// The source may lie inside this string's own buffer (MFC's memcpy is a memmove).
void CString::AssignCopy(int length, const char* text) {
    AllocBeforeWrite(length);
    std::memmove(data_, text, static_cast<std::size_t>(length));
    GetData()->dataLength = length;
    data_[length] = '\0';
}

const CString& CString::operator=(const CString& source) {
    if (data_ != source.data_) {
        if ((GetData()->refs < 0 && GetData() != DataNil()) || source.GetData()->refs < 0) {
            // One of them is locked: copy the characters.
            AssignCopy(source.GetData()->dataLength, source.data_);
        } else {
            Release();
            data_ = source.data_;
            Increment(&GetData()->refs);
        }
    }
    return *this;
}

const CString& CString::operator=(const char* text) {
    AssignCopy(SafeLength(text), text);
    return *this;
}

const CString& CString::operator=(char c) {
    AssignCopy(1, &c);
    return *this;
}

// Writes over data_ without releasing it (the callers do).
void CString::ConcatCopy(int length1, const char* text1, int length2, const char* text2) {
    int length = length1 + length2;
    if (length == 0) return;
    AllocBuffer(length);
    std::memcpy(data_, text1, static_cast<std::size_t>(length1));
    std::memcpy(data_ + length1, text2, static_cast<std::size_t>(length2));
}

CString __stdcall operator+(const CString& a, const CString& b) {
    CString s;
    s.ConcatCopy(a.GetData()->dataLength, a, b.GetData()->dataLength, b);
    return s;
}

CString __stdcall operator+(const CString& a, const char* b) {
    CString s;
    s.ConcatCopy(a.GetData()->dataLength, a, SafeLength(b), b);
    return s;
}

CString __stdcall operator+(const char* a, const CString& b) {
    CString s;
    s.ConcatCopy(SafeLength(a), a, b.GetData()->dataLength, b);
    return s;
}

CString __stdcall operator+(const CString& a, char b) {
    CString s;
    s.ConcatCopy(a.GetData()->dataLength, a, 1, &b);
    return s;
}

CString __stdcall operator+(char a, const CString& b) {
    CString s;
    s.ConcatCopy(1, &a, b.GetData()->dataLength, b);
    return s;
}

// In place when this buffer is not shared and has room, else into a new buffer (the old one released).
void CString::ConcatInPlace(int length, const char* text) {
    if (length == 0) return;
    if (GetData()->refs > 1 || GetData()->dataLength + length > GetData()->allocLength) {
        CStringData* old = GetData();
        ConcatCopy(GetData()->dataLength, data_, length, text);
        Release(old);
    } else {
        std::memcpy(data_ + GetData()->dataLength, text, static_cast<std::size_t>(length));
        GetData()->dataLength += length;
        data_[GetData()->dataLength] = '\0';
    }
}

const CString& CString::operator+=(const char* text) {
    ConcatInPlace(SafeLength(text), text);
    return *this;
}

const CString& CString::operator+=(char c) {
    ConcatInPlace(1, &c);
    return *this;
}

const CString& CString::operator+=(const CString& text) {
    ConcatInPlace(text.GetData()->dataLength, text.data_);
    return *this;
}

char* CString::GetBuffer(int minLength) {
    if (GetData()->refs > 1 || minLength > GetData()->allocLength) {
        CStringData* old = GetData();
        int oldLength = old->dataLength;
        if (minLength < oldLength) minLength = oldLength;
        AllocBuffer(minLength);
        std::memcpy(data_, old->data(), static_cast<std::size_t>(oldLength) + 1);
        GetData()->dataLength = oldLength;
        Release(old);
    }
    return data_;
}

void CString::ReleaseBuffer(int newLength) {
    CopyBeforeWrite();
    if (newLength == -1) newLength = StrLen(data_);
    GetData()->dataLength = newLength;
    data_[newLength] = '\0';
}

char* CString::GetBufferSetLength(int length) {
    GetBuffer(length);
    GetData()->dataLength = length;
    data_[length] = '\0';
    return data_;
}

char* CString::LockBuffer() {
    char* buffer = GetBuffer(0);
    GetData()->refs = -1;
    return buffer;
}

void CString::UnlockBuffer() {
    if (GetData() != DataNil()) GetData()->refs = 1;
}

// ---- CString: searching and extracting ----------------------------------------------------------

int CString::Find(char c) const { return Find(c, 0); }

int CString::Find(char c, int start) const {
    if (start >= GetData()->dataLength) return -1;
    const uchar* found = libcrt::Mbschr(U(data_ + start), static_cast<uchar>(c));
    return found ? static_cast<int>(reinterpret_cast<const char*>(found) - data_) : -1;
}

int CString::Find(const char* sub) const { return Find(sub, 0); }

int CString::Find(const char* sub, int start) const {
    if (start > GetData()->dataLength) return -1;
    const uchar* found = libcrt::Mbsstr(U(data_ + start), U(sub));
    return found ? static_cast<int>(reinterpret_cast<const char*>(found) - data_) : -1;
}

int CString::ReverseFind(char c) const {
    const uchar* found = libcrt::Mbsrchr(U(data_), static_cast<uchar>(c));
    return found ? static_cast<int>(reinterpret_cast<const char*>(found) - data_) : -1;
}

int CString::FindOneOf(const char* set) const {
    const uchar* found = libcrt::Mbspbrk(U(data_), U(set));
    return found ? static_cast<int>(reinterpret_cast<const char*>(found) - data_) : -1;
}

CString CString::Mid(int first) const { return Mid(first, GetData()->dataLength - first); }

CString CString::Mid(int first, int count) const {
    if (first < 0) first = 0;
    if (count < 0) count = 0;
    int length = GetData()->dataLength;
    if (first + count > length) count = length - first;
    if (first > length) count = 0;
    if (first == 0 && count == length) return *this;
    CString dest;
    AllocCopy(dest, count, first, 0);
    return dest;
}

CString CString::Right(int count) const {
    if (count < 0) count = 0;
    int length = GetData()->dataLength;
    if (count >= length) return *this;
    CString dest;
    AllocCopy(dest, count, length - count, 0);
    return dest;
}

CString CString::Left(int count) const {
    if (count < 0) count = 0;
    if (count >= GetData()->dataLength) return *this;
    CString dest;
    AllocCopy(dest, count, 0, 0);
    return dest;
}

// ---- CString: editing ---------------------------------------------------------------------------

void CString::MakeLower() {
    CopyBeforeWrite();
    libcrt::Mbslwr(U(data_));
}

// The destination steps by _mbsinc over the byte just copied (MFC's own way).
int CString::Remove(char c) {
    CopyBeforeWrite();
    char* source = data_;
    char* dest = data_;
    char* end = data_ + GetData()->dataLength;
    while (source < end) {
        if (*source != c) {
            *dest = *source;
            dest = Inc(dest);
        }
        source = Inc(source);
    }
    *dest = '\0';
    int count = static_cast<int>(source - dest);
    GetData()->dataLength -= count;
    return count;
}

// TrimRight: cut at the start of the last run of characters that match.
void CString::TrimRight() {
    CopyBeforeWrite();
    char* last = nullptr;
    for (char* p = data_; *p != '\0'; p = Inc(p)) {
        if (IsSpace(*p)) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    if (last) {
        *last = '\0';
        GetData()->dataLength = static_cast<int>(last - data_);
    }
}

void CString::TrimRight(char target) {
    CopyBeforeWrite();
    char* last = nullptr;
    for (char* p = data_; *p != '\0'; p = Inc(p)) {
        if (*p == target) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    if (last) {
        *last = '\0';
        GetData()->dataLength = static_cast<int>(last - data_);
    }
}

void CString::TrimRight(const char* targets) {
    CopyBeforeWrite();
    char* last = nullptr;
    for (char* p = data_; *p != '\0'; p = Inc(p)) {
        if (Contains(targets, *p)) {
            if (!last) last = p;
        } else {
            last = nullptr;
        }
    }
    if (last) {
        *last = '\0';
        GetData()->dataLength = static_cast<int>(last - data_);
    }
}

void CString::TrimLeft() {
    CopyBeforeWrite();
    const char* p = data_;
    while (IsSpace(*p)) p = Inc(p);
    if (p != data_) {
        int length = GetData()->dataLength - static_cast<int>(p - data_);
        std::memmove(data_, p, static_cast<std::size_t>(length) + 1);
        GetData()->dataLength = length;
    }
}

void CString::TrimLeft(char target) {
    CopyBeforeWrite();
    const char* p = data_;
    while (target == *p) p = Inc(p);
    if (p != data_) {
        int length = GetData()->dataLength - static_cast<int>(p - data_);
        std::memmove(data_, p, static_cast<std::size_t>(length) + 1);
        GetData()->dataLength = length;
    }
}

// ---- CString: formatting ------------------------------------------------------------------------

// FormatV first walks the format to estimate the longest result (the buffer it allocates, so its
// allocLength), then prints with the CRT's vsprintf.
void CString::FormatV(const char* format, va_list arguments) {
    constexpr int kForceAnsi = 0x10000;
    constexpr int kForceUnicode = 0x20000;
    constexpr int kForceInt64 = 0x40000;
    va_list saved = arguments;
    int maxLength = 0;
    for (const char* p = format; *p != '\0'; p = Inc(p)) {
        // A '%' and a "%%" count as text.
        if (*p != '%' || *(p = Inc(p)) == '%') {
            maxLength += static_cast<int>(libcrt::Mbclen(U(p)));
            continue;
        }
        int itemLength = 0;
        int width = 0;
        for (; *p != '\0'; p = Inc(p)) {
            if (*p == '#') maxLength += 2;   // "0x"
            else if (*p == '*') width = va_arg(arguments, int);
            else if (*p == '-' || *p == '+' || *p == '0' || *p == ' ') continue;
            else break;
        }
        if (width == 0) {
            width = libcrt::Atoi(p);
            for (; *p != '\0' && IsDigit(*p); p = Inc(p)) {}
        }
        int precision = 0;
        if (*p == '.') {
            p = Inc(p);
            if (*p == '*') {
                precision = va_arg(arguments, int);
                p = Inc(p);
            } else {
                precision = libcrt::Atoi(p);
                for (; *p != '\0' && IsDigit(*p); p = Inc(p)) {}
            }
        }
        int modifier = 0;
        if (libcrt::Mbsncmp(U(p), U("I64"), 3) == 0) {
            p += 3;
            modifier = kForceInt64;
        } else {
            switch (*p) {
            case 'h': modifier = kForceAnsi; p = Inc(p); break;
            case 'l': modifier = kForceUnicode; p = Inc(p); break;
            case 'F':
            case 'N':
            case 'L': p = Inc(p); break;
            default: break;
            }
        }
        switch (*p | modifier) {
        case 'c':
        case 'C':
        case 'c' | kForceAnsi:
        case 'C' | kForceAnsi:
        case 'c' | kForceUnicode:
        case 'C' | kForceUnicode:
            itemLength = 2;
            (void)va_arg(arguments, int);
            break;
        case 's':
        case 's' | kForceAnsi:
        case 'S' | kForceAnsi: {
            const char* text = va_arg(arguments, const char*);
            if (!text) {
                itemLength = 6;   // "(null)"
            } else {
                itemLength = StrLen(text);
                if (itemLength < 1) itemLength = 1;
            }
            break;
        }
        case 'S':
        case 's' | kForceUnicode:
        case 'S' | kForceUnicode: {
            const wchar_t* text = va_arg(arguments, const wchar_t*);
            if (!text) {
                itemLength = 6;
            } else {
                itemLength = static_cast<int>(libcrt::Wcslen(text));
                if (itemLength < 1) itemLength = 1;
            }
            break;
        }
        default: break;
        }
        if (itemLength != 0) {
            if (precision != 0 && precision < itemLength) itemLength = precision;
            if (itemLength < width) itemLength = width;
        } else {
            switch (*p) {
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
                if (modifier & kForceInt64) (void)va_arg(arguments, long long);
                else (void)va_arg(arguments, int);
                itemLength = 32;
                if (itemLength < width + precision) itemLength = width + precision;
                break;
            case 'e':
            case 'g':
            case 'G':
                (void)va_arg(arguments, double);
                itemLength = 128;
                if (itemLength < width + precision) itemLength = width + precision;
                break;
            case 'f': {
                // 312 == strlen("-1+(309 zeroes).") and 6 for the default precision.
                int size = precision + 318;
                if (width > size) size = width;
                std::vector<char> temp(static_cast<std::size_t>(size));
                double value = va_arg(arguments, double);
                gamecrt::sprintf(temp.data(), "%*.*f", width, precision + 6, value);
                itemLength = static_cast<int>(std::strlen(temp.data()));
                break;
            }
            case 'p':
                (void)va_arg(arguments, void*);
                itemLength = 32;
                if (itemLength < width + precision) itemLength = width + precision;
                break;
            case 'n':
                (void)va_arg(arguments, int*);
                break;
            default: break;
            }
        }
        maxLength += itemLength;
    }
    GetBuffer(maxLength);
    VSPrintF(data_, format, saved);
    ReleaseBuffer(-1);
}

void __cdecl CString::Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    FormatV(format, arguments);
    va_end(arguments);
}

// ---- Elements -----------------------------------------------------------------------------------

void __stdcall ConstructStrings(CString* elements, int count) {
    for (; count--; ++elements) ::new (static_cast<void*>(elements)) CString;
}

void __stdcall DestructStrings(CString* elements, int count) {
    for (; count--; ++elements) elements->~CString();
}

// ---- Registrations ------------------------------------------------------------------------------
//
// Constructors and the destructor are registered through shims (a member pointer cannot name them).

namespace shim {
CFixedAlloc* __fastcall FixedAllocConstruct(CFixedAlloc* self, void*, unsigned allocSize, unsigned blockSize) {
    return self->Construct(allocSize, blockSize);
}
CString* __fastcall TextConstruct(CString* self, void*, const char* text) { return ::new (self) CString(text); }
void __fastcall Destruct(CString* self) { self->~CString(); }
} // namespace shim

EI_REPLACE(0x006E6BCD, shim::FixedAllocConstruct);
EI_REPLACE_METHOD(0x006E6BF5, CFixedAlloc::Destruct);

EI_REWRITTEN(0x007009DE, shim::TextConstruct);
EI_REPLACE(0x00700970, shim::Destruct);

} // namespace mfc::impl
