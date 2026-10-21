#pragma once

// MFC 4.2's CString (strcore.cpp, strex.cpp) and the fixed-size block allocator its buffers come
// from, written out for the standalone exe (ticket S5, docs/notes/standalone.md section 2). Same
// layout and allocator as the copy linked into game.exe: every body is registered at its library
// address (mfc_string.cpp), so the forwarders in mfc.h and src/game reach it there, and
// verify_mfc_string.cpp checked it against the original in the hook build until S21d. Code that
// uses strings keeps calling mfc::CString (the forwarders) until C13 makes them direct calls.
//
// The library's helpers (AllocBuffer, ConcatCopy, ...) are public here so they can be registered.

#include "mfc.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstdarg>
#include <cstdint>

namespace mfc::impl {

// CFixedAlloc (0x28 bytes): hands out blocks of `allocSize` bytes, carved `blockSize` at a time from
// CPlex chunks and recycled through a free list, under a critical section. CString's buffers of up
// to 64, 128, 256 and 512 characters come from four of them.
class CFixedAlloc {
public:
    CFixedAlloc* Construct(unsigned allocSize, unsigned blockSize);   // 006E6BCD (the constructor; returns this)
    void Destruct();                                                // 006E6BF5 (the destructor)
    void FreeAll();                                                 // 006E6C09
    void* Alloc();                                                  // 006E6C31
    void Free(void* block);                                         // 006E6CC0

    unsigned AllocSize() const { return allocSize_; }
    unsigned BlockSize() const { return blockSize_; }

private:
    struct Node {
        Node* next;
    };
    unsigned allocSize_;        // +0x00
    unsigned blockSize_;        // +0x04
    CPlex* blocks_;             // +0x08
    Node* freeList_;            // +0x0C
    CRITICAL_SECTION lock_;     // +0x10
};
static_assert(sizeof(CFixedAlloc) == 0x28);

// The four string allocators (_afxAlloc64 ... _afxAlloc512, constructed by the static initialisers
// 00700722 ... 007007B5 with blocks of 64 entries).
inline CFixedAlloc& StringAlloc64() { return ei::GameData<CFixedAlloc>(0x007C8B80); }
inline CFixedAlloc& StringAlloc128() { return ei::GameData<CFixedAlloc>(0x007C8B58); }
inline CFixedAlloc& StringAlloc256() { return ei::GameData<CFixedAlloc>(0x007C8B30); }
inline CFixedAlloc& StringAlloc512() { return ei::GameData<CFixedAlloc>(0x007C8B08); }

// CStringData: the header before a string's characters. `refs` is -1 for the shared empty string
// (_afxDataNil) and for a locked buffer (LockBuffer).
struct CStringData {
    long refs;          // +0x00
    int dataLength;     // +0x04
    int allocLength;    // +0x08

    char* data() { return reinterpret_cast<char*>(this + 1); }
};
static_assert(sizeof(CStringData) == 12);

// _afxDataNil and _afxPchNil: the shared empty string's header and its characters (game data).
#ifdef EI_VECTORS_REPLAY
// ei_vectors has no game data: the empty string of its own CString (tests/vectors/replay_mfc.cpp).
CStringData* DataNil();
char* PchNil();
#else
inline CStringData* DataNil() { return ei::GameData<CStringData*>(0x00796E4C); }
inline char* PchNil() { return ei::GameData<char*>(0x00796E48); }
#endif

class CString {
public:
    CString() : data_(PchNil()) {}                  // inline in MFC (Init)
    CString(const CString& source);                 // 007006E5
    CString(const char* text);                      // 007009DE (a MAKEINTRESOURCE id loads the string)
    CString(const char* text, int length);          // 006FD4DE
    ~CString();                                     // 00700970

    const CString& operator=(const CString& source);    // 00700AA9
    const CString& operator=(const char* text);         // 00700AF9
    const CString& operator=(char c);                   // 006FD559
    const CString& operator+=(const CString& text);     // 00700D88
    const CString& operator+=(const char* text);        // 00700D4C
    const CString& operator+=(char c);                  // 00700D73

    int GetLength() const { return GetData()->dataLength; }
    bool IsEmpty() const { return GetData()->dataLength == 0; }
    operator const char*() const { return data_; }
    void Empty();                                   // 007008FB
    // CString::LoadString (00702B0E): string `id` of game.exe's table (mfc_resource.h); FALSE when it
    // is missing or empty (the string is then empty).
    BOOL LoadResourceString(unsigned id);

    char* GetBuffer(int minLength);                 // 00700DA0
    void ReleaseBuffer(int newLength);              // 00700DEF (-1: up to the terminator)
    char* GetBufferSetLength(int length);           // 00700E17
    char* LockBuffer();                             // 00700E6A
    void UnlockBuffer();                            // 00700E7C

    int Find(char c) const;                         // 00700E91
    int Find(char c, int start) const;              // 00700E9F
    int Find(const char* sub) const;                // 006FDBD9
    int Find(const char* sub, int start) const;     // 006FDBE7
    int ReverseFind(char c) const;                  // 006FDBB7
    int FindOneOf(const char* set) const;           // 00700ECC

    CString Mid(int first) const;                   // 006FD9B4
    CString Mid(int first, int count) const;        // 006FD9D7
    CString Left(int count) const;                  // 006FDAE9
    CString Right(int count) const;                 // 006FDA6D

    void MakeLower();                               // 00700EFE
    int Remove(char c);                             // 006FD963
    void TrimRight();                               // 006FE11F
    void TrimRight(char target);                    // 006FE0DD
    void TrimRight(const char* targets);            // 006FE08A
    void TrimLeft();                                // 006FE216
    void TrimLeft(char target);                     // 006FE1D3

    void __cdecl Format(const char* format, ...);   // 006FDF5F (`this` is the first stack argument)
    void FormatV(const char* format, va_list arguments);   // 006FDC12

    // The library's helpers.
    CStringData* GetData() const { return reinterpret_cast<CStringData*>(data_) - 1; }
    void Init() { data_ = PchNil(); }
    void AllocBuffer(int length);                                       // 007007DD
    void AllocCopy(CString& dest, int copyLength, int copyIndex, int extraLength) const;   // 0070099A
    void AssignCopy(int length, const char* text);                      // 00700A7C
    void ConcatCopy(int length1, const char* text1, int length2, const char* text2);   // 00700B61
    void ConcatInPlace(int length, const char* text);                   // 00700CED
    void CopyBeforeWrite();                                             // 00700919
    void AllocBeforeWrite(int length);                                  // 00700947
    void Release();                                                     // 007008A7
    static void __stdcall Release(CStringData* data);                   // 007008D8
    static void __fastcall FreeData(CStringData* data);                 // 0070085F

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

CString __stdcall operator+(const CString& a, const CString& b);    // 00700B9F
CString __stdcall operator+(const CString& a, const char* b);       // 00700C05
CString __stdcall operator+(const char* a, const CString& b);       // 00700C79
CString __stdcall operator+(const CString& a, char b);              // 006FD56E
CString __stdcall operator+(char a, const CString& b);              // 006FD5D0

// ConstructElements / DestructElements<CString> (AFXTEMPL.H, out of line): new elements point at
// the empty string; destroying releases each.
void __stdcall ConstructStrings(CString* elements, int count);      // 006FE268
void __stdcall DestructStrings(CString* elements, int count);       // 006FE296

} // namespace mfc::impl
