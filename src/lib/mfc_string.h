#pragma once

// MFC 4.2's CString (strcore.cpp, strex.cpp) and the fixed-size block allocator its buffers come
// from, written out for the standalone exe (docs/notes/standalone.md section 2). Same
// layout and allocator as the copy linked into game.exe: every body is registered at its library
// address (mfc_string.cpp).
// The library's helpers (AllocBuffer, ConcatCopy, ...) are public here so they can be registered.

#include "mfc.h"
#include "static_object.h"
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
    CFixedAlloc* Construct(unsigned allocSize, unsigned blockSize);   // (the constructor; returns this)
    void Destruct();                                                // (the destructor)
    void FreeAll();
    void* Alloc();
    void Free(void* block);

    unsigned AllocSize() const { return allocSize_; }
    unsigned BlockSize() const { return blockSize_; }

private:
    struct Node {
        Node* next;
    };
    unsigned allocSize_;
    unsigned blockSize_;
    CPlex* blocks_;
    Node* freeList_;
    CRITICAL_SECTION lock_;
};
static_assert(sizeof(CFixedAlloc) == 0x28);

#ifndef EI_VECTORS_REPLAY
// The four string allocators (_afxAlloc64 ... _afxAlloc512, blocks of 64 entries): each built by its static
// initialiser and destroyed by the exit function that registers (mfc_string.cpp). In game.exe's
// runs the harness builds them itself (tests/verify/harness_startup.cpp): our CString code there (the harness's, the
// vector recorders') takes its buffers from them, and game.exe's code frees such a buffer into its own allocator of
// the same size.
extern lib::StaticObject<CFixedAlloc> g_stringAlloc64;
extern lib::StaticObject<CFixedAlloc> g_stringAlloc128;
extern lib::StaticObject<CFixedAlloc> g_stringAlloc256;
extern lib::StaticObject<CFixedAlloc> g_stringAlloc512;
#endif

// CStringData: the header before a string's characters. `refs` is -1 for the shared empty string
// (_afxDataNil) and for a locked buffer (LockBuffer).
struct CStringData {
    long refs;
    int dataLength;
    int allocLength;

    char* data() { return reinterpret_cast<char*>(this + 1); }
};
static_assert(sizeof(CStringData) == 12);

// _afxDataNil and _afxPchNil: the shared empty string's header and its characters.
#ifdef EI_VECTORS_REPLAY
// ei_vectors has no game data: the empty string of its own CString (tests/vectors/replay_mfc.cpp).
CStringData* DataNil();
char* PchNil();
#else
// Ours (mfc_string.cpp); game.exe's own when this code runs in game.exe (the harness and the vector recorders handle
// game.exe's strings there: tests/verify/harness_startup.cpp sets it at static initialisation).
extern CStringData* g_dataNil;
inline CStringData* DataNil() { return g_dataNil; }
inline char* PchNil() { return g_dataNil->data(); }
#endif

class CString {
public:
    CString() : data_(PchNil()) {}                  // inline in MFC (Init)
    CString(const CString& source);
    CString(const char* text);                      // (a MAKEINTRESOURCE id loads the string)
    CString(const char* text, int length);
    ~CString();

    const CString& operator=(const CString& source);
    const CString& operator=(const char* text);
    const CString& operator=(char c);
    const CString& operator+=(const CString& text);
    const CString& operator+=(const char* text);
    const CString& operator+=(char c);

    int GetLength() const { return GetData()->dataLength; }
    bool IsEmpty() const { return GetData()->dataLength == 0; }
    operator const char*() const { return data_; }
    void Empty();
    // CString::LoadString: string `id` of game.exe's table (mfc_resource.h); FALSE when it
    // is missing or empty (the string is then empty).
    BOOL LoadResourceString(unsigned id);

    char* GetBuffer(int minLength);
    void ReleaseBuffer(int newLength);              // (-1: up to the terminator)
    char* GetBufferSetLength(int length);
    char* LockBuffer();
    void UnlockBuffer();

    int Find(char c) const;
    int Find(char c, int start) const;
    int Find(const char* sub) const;
    int Find(const char* sub, int start) const;
    int ReverseFind(char c) const;
    int FindOneOf(const char* set) const;

    CString Mid(int first) const;
    CString Mid(int first, int count) const;
    CString Left(int count) const;
    CString Right(int count) const;

    void MakeLower();
    int Remove(char c);
    void TrimRight();
    void TrimRight(char target);
    void TrimRight(const char* targets);
    void TrimLeft();
    void TrimLeft(char target);

    void __cdecl Format(const char* format, ...);   // (`this` is the first stack argument)
    void FormatV(const char* format, va_list arguments);

    // The library's helpers.
    CStringData* GetData() const { return reinterpret_cast<CStringData*>(data_) - 1; }
    void Init() { data_ = PchNil(); }
    void AllocBuffer(int length);
    void AllocCopy(CString& dest, int copyLength, int copyIndex, int extraLength) const;
    void AssignCopy(int length, const char* text);
    void ConcatCopy(int length1, const char* text1, int length2, const char* text2);
    void ConcatInPlace(int length, const char* text);
    void CopyBeforeWrite();
    void AllocBeforeWrite(int length);
    // The constructor from text in place and operator=(LPCWSTR) as
    // members, so they register by name; both return this.
    CString* ConstructText(const char* text);
    CString* AssignWide(const wchar_t* text);
    void Release();
    static void __stdcall Release(CStringData* data);
    static void __fastcall FreeData(CStringData* data);

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

CString __stdcall operator+(const CString& a, const CString& b);
CString __stdcall operator+(const CString& a, const char* b);
CString __stdcall operator+(const char* a, const CString& b);
CString __stdcall operator+(const CString& a, char b);
CString __stdcall operator+(char a, const CString& b);

// ConstructElements / DestructElements<CString> (AFXTEMPL.H, out of line): new elements point at
// the empty string; destroying releases each.
void __stdcall ConstructStrings(CString* elements, int count);
void __stdcall DestructStrings(CString* elements, int count);

} // namespace mfc::impl
