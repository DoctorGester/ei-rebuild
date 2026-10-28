// MFC 4.2's CString and CFixedAlloc (mfc_string.h).
#include "mfc_string.h"

#include "crt.h"
#include "gameapi.h"
#include "mfc_resource.h"
#include "game/game_data.h"

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mbstring.h>
#include <new>
#include <vector>

namespace mfc::impl {

#ifndef EI_VECTORS_REPLAY
namespace {

// The shared empty string: _afxInitData {-1, 0, 0} and its characters right after it.
struct NilString {
    CStringData data;
    char chars[4];
};
constinit NilString g_nilString = {{-1, 0, 0}, {}};

} // namespace

constinit CStringData* g_dataNil = &g_nilString.data;
#endif

namespace {

using uchar = unsigned char;

// The string helpers take the CRT's char arguments the way the library passes them.
const uchar* U(const char* p) { return reinterpret_cast<const uchar*>(p); }
uchar* U(char* p) { return reinterpret_cast<uchar*>(p); }
char* Inc(const char* p) { return const_cast<char*>(gamecrt::mbsinc(p)); }
// _tcschr with a char the library sign-extends (movsx) before the call.
bool Contains(const char* set, char c) { return _mbschr(U(set), static_cast<unsigned>(static_cast<int>(c))) != nullptr; }

// The reference count updates and lstrlen, the Windows functions (no route takes their import slots).
LONG Increment(long* refs) { return ::InterlockedIncrement(refs); }
LONG Decrement(long* refs) { return ::InterlockedDecrement(refs); }
int StrLen(const char* text) { return gameapi::lstrlenA(text); }

// SafeStrlen: lstrlen, 0 for null.
int SafeLength(const char* text) { return text ? StrLen(text) : 0; }

} // namespace


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

#ifndef EI_VECTORS_REPLAY
// The string allocators, zero at the start: each built by its static initialiser in the list's order (a
// block's size is the string's room, the header and the terminator, rounded up to 16 bytes) and destroyed by
// the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CFixedAlloc> g_stringAlloc512 = /*007C8B08*/ {};
constinit lib::StaticObject<CFixedAlloc> g_stringAlloc256 = /*007C8B30*/ {};
constinit lib::StaticObject<CFixedAlloc> g_stringAlloc128 = /*007C8B58*/ {};
constinit lib::StaticObject<CFixedAlloc> g_stringAlloc64 = /*007C8B80*/ {};

namespace {
constexpr unsigned kStringAllocBlock = 64;
void DestroyStringAlloc64() { g_stringAlloc64->Destruct(); }
void DestroyStringAlloc128() { g_stringAlloc128->Destruct(); }
void DestroyStringAlloc256() { g_stringAlloc256->Destruct(); }
void DestroyStringAlloc512() { g_stringAlloc512->Destruct(); }
} // namespace

void ConstructStringAlloc64() {
    g_stringAlloc64->Construct(0x50, kStringAllocBlock);
    std::atexit(DestroyStringAlloc64);
}
void ConstructStringAlloc128() {
    g_stringAlloc128->Construct(0x90, kStringAllocBlock);
    std::atexit(DestroyStringAlloc128);
}
void ConstructStringAlloc256() {
    g_stringAlloc256->Construct(0x110, kStringAllocBlock);
    std::atexit(DestroyStringAlloc256);
}
void ConstructStringAlloc512() {
    g_stringAlloc512->Construct(0x210, kStringAllocBlock);
    std::atexit(DestroyStringAlloc512);
}

#endif

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
    data = gamecrt::operator_new_bytes_as<CStringData>(sizeof(CStringData) + static_cast<std::size_t>(length) + 1);
    data->allocLength = length;
#else
    if (length <= 64) {
        data = static_cast<CStringData*>(g_stringAlloc64->Alloc());
        data->allocLength = 64;
    } else if (length <= 128) {
        data = static_cast<CStringData*>(g_stringAlloc128->Alloc());
        data->allocLength = 128;
    } else if (length <= 256) {
        data = static_cast<CStringData*>(g_stringAlloc256->Alloc());
        data->allocLength = 256;
    } else if (length <= 512) {
        data = static_cast<CStringData*>(g_stringAlloc512->Alloc());
        data->allocLength = 512;
    } else {
        data = gamecrt::operator_new_bytes_as<CStringData>(sizeof(CStringData) + static_cast<std::size_t>(length) + 1);
        data->allocLength = length;
    }
#endif
    data->refs = 1;
    data->data()[length] = '\0';
    data->dataLength = length;
    data_ = data->data();
}

void CString::FreeData(CStringData* data) {
#ifdef EI_VECTORS_REPLAY
    gamecrt::operator_delete(data);   // AllocBuffer's
#else
    switch (data->allocLength) {
    case 64: g_stringAlloc64->Free(data); break;
    case 128: g_stringAlloc128->Free(data); break;
    case 256: g_stringAlloc256->Free(data); break;
    case 512: g_stringAlloc512->Free(data); break;
    default: gamecrt::operator_delete(data); break;
    }
#endif
}

void CString::Release() {
    if (GetData() == DataNil()) return;
    if (Decrement(&GetData()->refs) <= 0) FreeData(GetData());
    Init();
}

void CString::Release(CStringData* data) {
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

// The source may lie inside this string's own buffer (MFC's memcpy is a memmove).
void CString::AssignCopy(int length, const char* text) {
    AllocBeforeWrite(length);
    std::memmove(data_, text, static_cast<std::size_t>(length));
    GetData()->dataLength = length;
    data_[length] = '\0';
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

CString operator+(const CString& a, const char* b) {
    CString s;
    s.ConcatCopy(a.GetData()->dataLength, a, SafeLength(b), b);
    return s;
}

CString operator+(const char* a, const CString& b) {
    CString s;
    s.ConcatCopy(SafeLength(a), a, b.GetData()->dataLength, b);
    return s;
}

CString operator+(const CString& a, char b) {
    CString s;
    s.ConcatCopy(a.GetData()->dataLength, a, 1, &b);
    return s;
}

CString operator+(char a, const CString& b) {
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

// ---- CString: searching and extracting ----------------------------------------------------------

int CString::Find(char c) const { return Find(c, 0); }

int CString::Find(char c, int start) const {
    if (start >= GetData()->dataLength) return -1;
    const uchar* found = _mbschr(U(data_ + start), static_cast<uchar>(c));
    return found ? static_cast<int>(found - U(data_)) : -1;
}

int CString::Find(const char* sub) const { return Find(sub, 0); }

int CString::Find(const char* sub, int start) const {
    if (start > GetData()->dataLength) return -1;
    const uchar* found = _mbsstr(U(data_ + start), U(sub));
    return found ? static_cast<int>(found - U(data_)) : -1;
}

// ---- CString: editing ---------------------------------------------------------------------------

void CString::MakeLower() {
    CopyBeforeWrite();
    libcrt::Mbslwr(U(data_));
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

} // namespace mfc::impl
