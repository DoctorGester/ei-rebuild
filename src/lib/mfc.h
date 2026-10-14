#pragma once

// MFC 4.2 classes statically linked into game.exe, used by rewritten code through the original
// implementations (config/library.csv). Layouts match MFC so objects can be shared with original code.

#include "crt.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mfc {

using BOOL = int;

// CString: a single pointer to the character data; the reference count, length and capacity live
// in a header just before it. An empty string points at the shared afxEmptyString data.
class CString {
public:
    CString();                                    // inline in MFC: m_pchData = afxEmptyString
    CString(const char* s);                       // 007009DE
    CString(const CString& s);                    // 007006E5
    ~CString();                                   // 00700970
    const CString& operator=(const char* s);      // 00700AF9
    const CString& operator=(const CString& s);   // 00700AA9

    // 006FDF5F. printf-style; MFC declares it __cdecl, so `this` is the first stack argument.
    void __cdecl Format(const char* format, ...);
    void FormatV(const char* format, va_list arguments);   // 006FDC12

    void Empty();                                 // 007008FB
    char* GetBufferSetLength(int length);         // 00700E17
    char* GetBuffer(int minLength);               // 00700DA0
    void ReleaseBuffer(int newLength);            // 00700DEF (-1: up to the terminator)
    int GetLength() const { return reinterpret_cast<const int*>(data_)[-2]; }   // CStringData::nDataLength
    void MakeUpper();                             // 00700EFE (calls _mbsupr)
    void AssignChar(char c);                      // 006FD559 operator=(TCHAR)
    void TrimRight(const char* targets);          // 006FE08A TrimRight(LPCTSTR)
    const CString& operator+=(const CString& s);  // 00700D88
    const CString& operator+=(const char* s);     // 00700D4C
    CString Left(int count) const;                // 006FDAE9 (RET 8: result slot, count)
    CString Right(int count) const;               // 006FDA6D Right(nCount) (RET 8: result slot, count; Mid(nFirst) is 006FD9B4)
    CString Mid(int first) const;                 // 006FD9B4 Mid(nFirst) (RET 8: result slot, first)
    CString Mid(int first, int count) const;      // 006FD9D7 Mid(nFirst, nCount) (RET 0xC)
    int Remove(char ch);                          // 006FD963 Remove(chRemove) (RET 4; the count removed)
    int FindOneOf(const char* set) const;         // 00700ECC
    int ReverseFind(char ch) const;               // 006FDBB7 (RET 4; -1 when absent)
    int Find(const char* sub) const;              // 006FDBD9 Find(LPCTSTR) (RET 4; -1 when absent)
    int Find(const char* sub, int start) const;   // 006FDBE7 Find(LPCTSTR, nStart) (RET 8)

    operator const char*() const { return data_; }

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

// CFile (0x10 bytes): vtable, OS handle, close-on-delete flag, file name. The game calls these
// methods non-virtually, so the forwarders are plain members.
class CFile {
public:
    enum OpenFlags { modeRead = 0 };

    CFile();                                                        // 00701036
    ~CFile();                                                       // 0070112B
    BOOL Open(const char* path, unsigned flags, void* error);       // 007011E8
    unsigned GetLength();                                           // 007014E2
    unsigned Read(void* buffer, unsigned count);                    // 00701305
    void Close();                                                   // 00701403
    // The OS file handle (m_hFile; INVALID_HANDLE_VALUE when closed).
    void* Handle() const { return handle_; }

private:
    void* vtable_;
    void* handle_;
    BOOL closeOnDelete_;
    char* fileName_;    // CString; raw here so the naked forwarders have no members to construct
};
static_assert(sizeof(CFile) == 0x10);

// The virtual slots every MFC 4.2 CObject has (release build: no AssertValid / Dump), for objects
// whose layout keeps the vtable as a plain field (the CArray / CList / CMap instantiations below,
// CDib) when a caller deletes them through their vtable, as the original does. Declared only.
class __declspec(novtable) CObjectVirtuals {
public:
    virtual const void* GetRuntimeClass() const;        // 0
    virtual void* DeletingDestructor(unsigned flags);   // 1: the destructor, then delete with bit 0
    virtual void Serialize(void* archive);              // 2: a CArchive*
};

// The virtual slots of MFC 4.2's CFile (vtable 00745DF4) for callers that dispatch through a
// file's vtable (the original does; the verify build hands some loaders a proxy file). CFile above
// calls the bodies directly. Declared only: a devirtualised call would not link. Slots declared
// `void Name()` keep their place in the order only.
class __declspec(novtable) CFileVirtuals {
public:
    virtual void GetRuntimeClass();                                    // 0: 0070BE58
    virtual void* DeletingDestructor(unsigned flags);                  // 1: 00701050
    virtual void Serialize();                                          // 2: CObject's (empty)
    virtual unsigned long GetPosition() const;                         // 3: 007013B9
    virtual void GetFileName();                                        // 4
    virtual void GetFileTitle();                                       // 5
    virtual void GetFilePath();                                        // 6
    virtual void SetFilePath();                                        // 7
    virtual BOOL Open(const char* path, unsigned flags, void* error);  // 8: 007011E8
    virtual void Duplicate();                                          // 9
    virtual long Seek(long offset, unsigned from);                     // 10: 0070138A
    virtual void SetLength(unsigned long length);                      // 11
    virtual unsigned long GetLength() const;                           // 12: 007014E2
    virtual unsigned Read(void* buffer, unsigned count);               // 13: 00701305
    virtual void Write(const void* buffer, unsigned count);            // 14: 0070133F
    virtual void LockRange();                                          // 15
    virtual void UnlockRange();                                        // 16
    virtual void Abort();                                              // 17
    virtual void Flush();                                              // 18
    virtual void Close();                                              // 19: 00701403
};

// CFileFind (0x1C bytes).
class CFileFind {
public:
    CFileFind();                                                    // 006E6370
    ~CFileFind();                                                   // 006E63AD
    BOOL FindFile(const char* pattern, unsigned long flags);       // 006E6433
    unsigned long GetLength() const;                                // 006E68AA (the found file's size, low dword)
    CString GetFilePath() const;                                    // 006E6722 (RET 4: the result slot)

private:
    std::uint8_t data_[0x1C - 0];
};
static_assert(sizeof(CFileFind) == 0x1C);

// The virtual slots of MFC 4.2's CFileFind (vtable 00745EE8) for callers that dispatch through it
// as the original does; declared only. Slots declared `void Name()` keep their place only (8-13
// are the six GetLast*Time / GetCreationTime overloads).
class __declspec(novtable) CFileFindVirtuals {
public:
    virtual void GetRuntimeClass();                         // 0: 0070BE5E
    virtual void DeletingDestructor();                      // 1: 006E6391
    virtual void Serialize();                               // 2
    virtual void GetFileName();                             // 3: 006E684B
    virtual void GetFilePath();                             // 4: 006E6722
    virtual void GetFileTitle();                            // 5: 006E67BA
    virtual void GetFileURL();                              // 6
    virtual void GetRoot();                                 // 7
    virtual void GetTime8();                                // 8
    virtual void GetTime9();                                // 9
    virtual void GetTime10();                               // 10
    virtual void GetTime11();                               // 11
    virtual void GetTime12();                               // 12
    virtual void GetTime13();                               // 13
    virtual BOOL MatchesMask(unsigned long mask) const;     // 14: 006E6513 (file attribute bits)
    virtual BOOL IsDots() const;                            // 15: 006E661A
};

// CTime (a time_t). No constructor, so it can be a plain local: Construct is the constructor.
class CTime {
public:
    // 006FE3BB (RET 0x1C). CTime(year, month, day, hour, minute, second, dst) through mktime.
    // Returns this.
    CTime* Construct(int year, int month, int day, int hour, int minute, int second, int dst);

    long time_;
};
static_assert(sizeof(CTime) == 4);

// CPlex: the block allocator behind MFC's lists and maps.
struct CPlex {
    CPlex* next;
    // followed by the block's elements
};
CPlex* __stdcall PlexCreate(CPlex*& head, unsigned count, unsigned elementSize);   // 006FE8EF
void __fastcall PlexFreeDataChain(CPlex* head);                                    // 006FE90F

// The game's CPtrList vtable (lists the code builds on the stack use blocks of 10).
inline void* PtrListVtable() { return reinterpret_cast<void*>(0x0073D454); }
// The game's CDWordArray / CArray<int> vtable (lists of ids built on the stack).
inline void* DWordArrayVtable() { return reinterpret_cast<void*>(0x0073C3D0); }

// ---- Collection templates (AFXTEMPL.H) ------------------------------------------------------------
//
// The game's CArray / CList / CTypedPtrList instantiations share one layout each and differ only in
// the element type and the vtable, so one template serves all of them (docs/notes/core.md, "Shared
// containers"). The vtable is a plain field: an object the original constructs keeps the one its
// constructor wrote, and code that builds one itself passes the instantiation's vtable. The
// templates have no constructors, so they can sit in layouts and be brace-initialised.
//
// The member functions are MFC 4.2's, written once: an instantiation the game calls out of line can
// either be registered on these members or keep forwarding to its original address.

// MFC 4.2's default ConstructElements / DestructElements for element types without constructors:
// new elements are zero-filled (the bitwise zeroing before the placement new), nothing is destroyed.
// An instantiation whose elements the original constructs out of line (CString, records with
// constructors) passes its own policy with the same two static functions.
template <typename T>
struct PlainElements {
    static void Construct(T* elements, int count) {
        std::memset(static_cast<void*>(elements), 0, static_cast<std::size_t>(count) * sizeof(T));
    }
    static void Destruct(T*, int) {}
};

// No construction at all: the engine's own arrays and the instantiations the game compiled without
// the zeroing (new slots keep the heap's bytes).
template <typename T>
struct RawElements {
    static void Construct(T*, int) {}
    static void Destruct(T*, int) {}
};

// CArray<T, ARG> (0x14 bytes): vtable, data, size, allocated size, growth step.
// MFC 4.2 CArchive, only what the CArray serialisers use: +0x14 the mode (bit 0: loading).
class CArchive {
public:
    bool IsLoading() const { return (mode_ & 1) != 0; }
    unsigned Read(void* buffer, unsigned bytes);          // 00708832
    void Write(const void* buffer, unsigned bytes);       // 00708941
    unsigned long ReadCount();                            // 00708B3D
    void WriteCount(unsigned long count);                 // 00708B0F
    void Flush();                                         // 007089D1
    CFile* File() const { return file_; }

private:
    std::uint8_t unknown_00[0x14];
    unsigned mode_;                 // +0x14
    std::uint8_t unknown_18[8];
    CFile* file_;                   // +0x20
};

template <typename T, typename Elements = PlainElements<T>>
struct CArray {
    void* vtable;
    T* data;        // +0x04 operator new storage (new BYTE[])
    int size;       // +0x08
    int maxSize;    // +0x0C
    int growBy;     // +0x10 0 = size / 8 clamped to 4..1024

    T& operator[](int index) { return data[index]; }
    const T& operator[](int index) const { return data[index]; }
    int GetSize() const { return size; }

    // The constructor body: the instantiation's vtable, empty (e.g. CByteArray's 006FF5EA).
    CArray* Construct(std::uintptr_t vtableAddress) {
        vtable = reinterpret_cast<void*>(vtableAddress);
        data = nullptr;
        growBy = 0;
        maxSize = 0;
        size = 0;
        return this;
    }

    // SetSize(newSize, growBy): 0 frees the storage; growing past maxSize reallocates to
    // max(newSize, maxSize + step), copying the old elements with memcpy and constructing only the
    // new ones; the old storage is freed without destroying anything. growBy -1 keeps the step.
    void SetSize(int newSize, int newGrowBy = -1) {
        if (newGrowBy != -1) growBy = newGrowBy;
        if (newSize == 0) {
            if (data) {
                Elements::Destruct(data, size);
                gamecrt::operator_delete(data);
                data = nullptr;
            }
            size = maxSize = 0;
        } else if (!data) {
            data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(newSize) * sizeof(T)));
            Elements::Construct(data, newSize);
            size = maxSize = newSize;
        } else if (newSize <= maxSize) {
            if (newSize > size) Elements::Construct(data + size, newSize - size);
            else if (size > newSize) Elements::Destruct(data + newSize, size - newSize);
            size = newSize;
        } else {
            int step = growBy;
            if (step == 0) {
                step = size / 8;
                step = step < 4 ? 4 : step > 1024 ? 1024 : step;
            }
            int newMax = newSize < maxSize + step ? maxSize + step : newSize;
            T* fresh = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(newMax) * sizeof(T)));
            std::memcpy(static_cast<void*>(fresh), data, static_cast<std::size_t>(size) * sizeof(T));
            Elements::Construct(fresh + size, newSize - size);
            gamecrt::operator_delete(data);
            data = fresh;
            size = newSize;
            maxSize = newMax;
        }
    }

    void RemoveAll() { SetSize(0, -1); }

    // Serialize (slot 2 of each instantiation's CObject vtable; CObject::Serialize is empty): the
    // count (ReadCount then SetSize when loading), then the elements as raw bytes (MFC's default
    // SerializeElements).
    void Serialize(CArchive* ar) {
        if (!ar->IsLoading()) ar->WriteCount(static_cast<unsigned long>(size));
        else SetSize(static_cast<int>(ar->ReadCount()), -1);
        unsigned bytes = static_cast<unsigned>(size) * static_cast<unsigned>(sizeof(T));
        if (!ar->IsLoading()) ar->Write(data, bytes);
        else ar->Read(data, bytes);
    }

    void SetAtGrow(int index, const T& value) {
        if (index >= size) SetSize(index + 1, -1);
        data[index] = value;
    }

    // Add: SetAtGrow at the end; returns the new element's index.
    int Add(const T& value) {
        int index = size;
        SetAtGrow(index, value);
        return index;
    }

    // InsertAt: grows by `count`, moves the tail up with memmove (the slots it vacates are
    // destroyed and constructed again), then assigns `value` to each new slot.
    void InsertAt(int index, const T& value, int count = 1) {
        if (index >= size) {
            SetSize(index + count, -1);
        } else {
            int oldSize = size;
            SetSize(size + count, -1);
            Elements::Destruct(data + oldSize, count);
            std::memmove(static_cast<void*>(data + index + count), data + index,
                         static_cast<std::size_t>(oldSize - index) * sizeof(T));
            Elements::Construct(data + index, count);
        }
        while (count--) data[index++] = value;
    }

    // RemoveAt: destroys `count` elements and moves the tail down with memmove; the storage stays.
    void RemoveAt(int index, int count = 1) {
        int moveCount = size - (index + count);
        Elements::Destruct(data + index, count);
        if (moveCount)
            std::memmove(static_cast<void*>(data + index), data + index + count,
                         static_cast<std::size_t>(moveCount) * sizeof(T));
        size -= count;
    }

    // Copy (operator= of the elements): SetSize to the source's size, then assign element by element.
    void Copy(const CArray& source) {
        SetSize(source.size, -1);
        for (int i = 0; i < source.size; ++i) data[i] = source.data[i];
    }

    // The destructor's body (~CArray): destroy the elements and free the storage. The vtable
    // writes around it are the caller's.
    void FreeData() {
        if (data) {
            Elements::Destruct(data, size);
            gamecrt::operator_delete(data);
        }
    }
};
static_assert(sizeof(CArray<int>) == 0x14);

// CList<T, ARG> (0x1C bytes): vtable, head, tail, count, free nodes, CPlex blocks, block size. A
// node is next, prev, then the element. Nodes are carved `blockSize` at a time and recycled
// through the free list; removing the last node frees every block (RemoveAll).
template <typename T, typename Elements = PlainElements<T>>
struct CList {
    struct Node {
        Node* next;
        Node* prev;
        T data;
    };

    void* vtable;
    Node* head;         // +0x04
    Node* tail;         // +0x08
    int count;          // +0x0C
    Node* free;         // +0x10
    CPlex* blocks;      // +0x14
    int blockSize;      // +0x18

    int GetCount() const { return count; }
    bool IsEmpty() const { return count == 0; }

    // NewNode: take a node from the free list, carving a new block of `blockSize` when empty (the
    // block's nodes are pushed last to first, so the first node is used first).
    Node* NewNode(Node* prev, Node* next) {
        if (!free) {
            CPlex* block = PlexCreate(blocks, static_cast<unsigned>(blockSize), sizeof(Node));
            Node* node = reinterpret_cast<Node*>(block + 1) + (blockSize - 1);
            for (int i = blockSize - 1; i >= 0; --i, --node) {
                node->next = free;
                free = node;
            }
        }
        Node* node = free;
        free = free->next;
        node->prev = prev;
        node->next = next;
        ++count;
        Elements::Construct(&node->data, 1);
        return node;
    }

    // FreeNode: return to the free list; the last node frees all blocks (RemoveAll).
    void FreeNode(Node* node) {
        Elements::Destruct(&node->data, 1);
        node->next = free;
        free = node;
        if (--count == 0) RemoveAll();
    }

    void RemoveAll() {
        for (Node* n = head; n; n = n->next) Elements::Destruct(&n->data, 1);
        count = 0;
        free = nullptr;
        tail = nullptr;
        head = nullptr;
        PlexFreeDataChain(blocks);
        blocks = nullptr;
    }

    Node* AddHead(const T& value) {
        Node* node = NewNode(nullptr, head);
        node->data = value;
        if (head) head->prev = node;
        else tail = node;
        head = node;
        return node;
    }

    Node* AddTail(const T& value) {
        Node* node = NewNode(tail, nullptr);
        node->data = value;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        return node;
    }

    T RemoveHead() {
        Node* old = head;
        T value = old->data;
        head = old->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        FreeNode(old);
        return value;
    }

    T RemoveTail() {
        Node* old = tail;
        T value = old->data;
        tail = old->prev;
        if (tail) tail->next = nullptr;
        else head = nullptr;
        FreeNode(old);
        return value;
    }

    Node* InsertBefore(Node* position, const T& value) {
        if (!position) return AddHead(value);
        Node* node = NewNode(position->prev, position);
        node->data = value;
        if (position->prev) position->prev->next = node;
        else head = node;
        position->prev = node;
        return node;
    }

    Node* InsertAfter(Node* position, const T& value) {
        if (!position) return AddTail(value);
        Node* node = NewNode(position, position->next);
        node->data = value;
        if (position->next) position->next->prev = node;
        else tail = node;
        position->next = node;
        return node;
    }

    void RemoveAt(Node* node) {
        if (node == head) head = node->next;
        else node->prev->next = node->next;
        if (node == tail) tail = node->prev;
        else node->next->prev = node->prev;
        FreeNode(node);
    }

    // Find: the first node from the head whose element equals `value` (a template, so a pointer
    // list can be searched with a pointer to const).
    template <typename V>
    Node* Find(const V& value) const {
        for (Node* n = head; n; n = n->next)
            if (n->data == value) return n;
        return nullptr;
    }

    // FindIndex: the index-th node from the head, or null.
    Node* FindIndex(int index) const {
        if (index >= count || index < 0) return nullptr;
        Node* n = head;
        while (index--) n = n->next;
        return n;
    }
};
static_assert(sizeof(CList<int>) == 0x1C);

// CMap<K, ARG_K, V, ARG_V> (0x1C bytes): vtable, hash table, table size, count, free
// associations, CPlex blocks, block size. An association is next, hash, key, value (dword packed,
// so a 64-bit key sits at +8). Hashing and the association pool stay with each instantiation's own
// GetAssocAt / InitHashTable / NewAssoc (called through their addresses); the walk and RemoveAll,
// which the game inlines, are written here.
#pragma pack(push, 4)
template <typename K, typename V>
struct CMapAssoc {
    CMapAssoc* next;
    std::uint32_t hash;
    K key;
    V value;
};
#pragma pack(pop)

template <typename K, typename V>
struct CMap {
    using Assoc = CMapAssoc<K, V>;

    void* vtable;
    Assoc** table;          // +0x04
    std::uint32_t hashSize; // +0x08 (17 until InitHashTable)
    int count;              // +0x0C
    Assoc* freeList;        // +0x10
    CPlex* blocks;          // +0x14
    int blockSize;          // +0x18

    // GetNextAssoc as inlined: the association after `assoc` (the next in its chain, else the head
    // of a later bucket), or null; null starts at the first bucket.
    Assoc* Next(const Assoc* assoc) const {
        if (assoc && assoc->next) return assoc->next;
        for (std::uint32_t b = assoc ? assoc->hash + 1 : 0; b < hashSize; ++b)
            if (table[b]) return table[b];
        return nullptr;
    }

    // RemoveAll for elements without destructors: the table deleted (even when null), the pool
    // and the blocks freed.
    void RemoveAll() {
        gamecrt::operator_delete(table);
        table = nullptr;
        count = 0;
        freeList = nullptr;
        PlexFreeDataChain(blocks);
        blocks = nullptr;
    }
};
static_assert(sizeof(CMap<int, int>) == 0x1C);
static_assert(sizeof(CMapAssoc<std::uint64_t, std::uint32_t>) == 0x14);

// CStringArray (afxcoll, 0x14 bytes): CArray's layout over CStrings. Its members are MFC's own
// code, reached through their addresses. No constructor, so it can sit in layouts:
// Construct / Destruct are the constructor and destructor (console::CStringArray is the local
// version that calls them).
struct CStringArray {
    void* vtable;       // written by Construct
    CString* data;      // +0x04
    int size;           // +0x08
    int maxSize;        // +0x0C
    int growBy;         // +0x10

    void Construct();                                   // 006FE925
    void Destruct();                                    // 006FE958
    void SetSize(int newSize, int newGrowBy = -1);      // 006FE9B8
    void SetAtGrow(int index, const CString& value);    // 006FEBFA
    void SetAtGrow(int index, const char* text);        // 006FEBCE
    void RemoveAt(int index, int count = 1);            // 006FED3C
    void Copy(const CStringArray& source);              // 006FEB61
    int Append(const CStringArray& source);             // 006FEB01: the old size

    int GetSize() const { return size; }
    CString& operator[](int index) { return data[index]; }
    const CString& operator[](int index) const { return data[index]; }
};
static_assert(sizeof(CStringArray) == 0x14);

// CTypedPtrList<CPtrList, T*>: a CPtrList (the non-template list of void*, same layout) with typed
// elements. The game inlines its operations; they are CList<T*>'s (a new node's data is zeroed,
// nothing destroyed).
template <typename T>
using CTypedPtrList = CList<T*>;

// CWinThread (AfxBeginThread's result, 007022A0); only m_hThread is named.
struct CWinThread {
    std::uint8_t unknown00[0x28];
    void* thread;   // +0x28 m_hThread
};

CString __stdcall operator+(const CString& a, const char* b);   // 00700C05
CString __stdcall operator+(const char* a, const CString& b);   // 00700C79
CString __stdcall operator+(const CString& a, const CString& b); // 00700B9F

} // namespace mfc
