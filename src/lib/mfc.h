#pragma once

// MFC 4.2 classes the game links statically, in MFC's layouts (config/library.csv).

#include "crt.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <string>

namespace mfc {

using BOOL = int;

// CString: a single pointer to the character data; the reference count, length and capacity live
// in a header just before it. An empty string points at the shared afxEmptyString data.
class CString {
public:
    CString();
    CString(const char* s);
    CString(const char* s, int length);
    CString(const CString& s);
    // A bridge while the game's text is half std::string (str.h): the same characters.
    CString(const std::string& s) : CString(s.c_str()) {}
    ~CString();
    const CString& operator=(const char* s);
    const CString& operator=(const CString& s);

    // Printf-style; MFC declares it __cdecl, so `this` is the first stack argument.
    void __cdecl Format(const char* format, ...);
    void FormatV(const char* format, va_list arguments);

    void Empty();
    char* GetBufferSetLength(int length);
    char* GetBuffer(int minLength);
    void ReleaseBuffer(int newLength);            // (-1: up to the terminator)
    int GetLength() const { return reinterpret_cast<const int*>(data_)[-2]; }
    void MakeLower();
    void AssignChar(char c);
    void TrimRight(const char* targets);
    void TrimRight();
    void TrimRight(char target);
    void TrimLeft();
    void TrimLeft(char target);
    const CString& operator+=(char c);
    const CString& operator+=(const CString& s);
    const CString& operator+=(const char* s);
    CString Left(int count) const;
    CString Right(int count) const;
    CString Mid(int first) const;
    CString Mid(int first, int count) const;
    int Remove(char ch);                          // the count removed
    int FindOneOf(const char* set) const;
    int ReverseFind(char ch) const;               // -1 when absent
    int Find(const char* sub) const;              // -1 when absent
    int Find(const char* sub, int start) const;
    int Find(char c) const;                       // -1 when absent
    int Find(char c, int start) const;

    operator const char*() const { return data_; }
    operator std::string_view() const { return std::string_view(data_, static_cast<std::size_t>(GetLength())); }
    // The characters (LPCTSTR), for printf-style arguments and other places the conversion is not
    // implicit (MFC 7's name).
    const char* GetString() const { return data_; }

    // Storage for a result built in place (mfc.cpp): the pointer is left unset.
    enum Unset { kUnset };
    explicit CString(Unset) {}

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

namespace impl { class CString; }
// The implementation's view of `s` (the two share the layout; mfc.cpp), for the implementation's members that
// take a string.
impl::CString& ImplOf(CString& s);

class CFileVirtuals;

// CFile: vtable, OS handle, close-on-delete flag, file name. The game calls these methods non-virtually, so
// they are plain members.
class CFile {
public:
    enum OpenFlags { modeRead = 0 };

    CFile();
    // CFile(path, flags): opens the file, throwing a CFileException* when it cannot.
    CFile(const char* path, unsigned flags);
    ~CFile();
    BOOL Open(std::string_view path, unsigned flags, void* error);
    unsigned GetLength();
    unsigned Read(void* buffer, unsigned count);
    void Write(const void* buffer, unsigned count);
    void Close();
    // The OS file handle (m_hFile; INVALID_HANDLE_VALUE when closed).
    void* Handle() const { return handle_; }
    CFileVirtuals* Virtuals();

private:
    void* vtable_;
    void* handle_;
    BOOL closeOnDelete_;
    char* fileName_;
};
static_assert(sizeof(CFile) == 0x10);

// The virtual slots of MFC 4.2's CFile, for callers that dispatch through a file's vtable (the verify build
// hands some loaders a proxy file). Slots declared `void Name()` only keep their place.
class __declspec(novtable) CFileVirtuals {
public:
    virtual void GetRuntimeClass();
    virtual void* DeletingDestructor(unsigned flags);
    virtual void Serialize();                                          // CObject's (empty)
    virtual unsigned long GetPosition() const;
    virtual void GetFileName();
    virtual void GetFileTitle();
    virtual void GetFilePath();
    virtual void SetFilePath();
    virtual BOOL Open(const char* path, unsigned flags, void* error);
    virtual void Duplicate();
    virtual long Seek(long offset, unsigned from);
    virtual void SetLength(unsigned long length);
    virtual unsigned long GetLength() const;
    virtual unsigned Read(void* buffer, unsigned count);
    virtual void Write(const void* buffer, unsigned count);
    virtual void LockRange();
    virtual void UnlockRange();
    virtual void Abort();
    virtual void Flush();
    virtual void Close();
    // (0: no buffer of its own; CArchive reads and writes through the file then).
    virtual unsigned GetBufferPtr(unsigned command, unsigned count, void** start, void** end);
};
inline CFileVirtuals* CFile::Virtuals() { return reinterpret_cast<CFileVirtuals*>(this); }

// The virtual slots every MFC 4.2 CObject has, for callers that dispatch through an object's vtable (the
// inlined DestructElements calls slot 1 with 0; a CList or a CDib is deleted through slot 1 with 1).
class __declspec(novtable) CObjectVirtuals {
public:
    virtual void GetRuntimeClass();
    virtual void* DeletingDestructor(unsigned flags);
    virtual void Serialize();
};

class CFileFind {
public:
    CFileFind();
    ~CFileFind();
    BOOL FindFile(std::string_view pattern, unsigned long flags);
    unsigned long GetLength() const;                                // (the found file's size, low dword)
    CString GetFilePath() const;
    CString GetFileName() const;
    // FindNextFile / GetFileTitle (Windows' headers define both names as macros).
    CString GetTitle() const;
    BOOL FindNext();
    BOOL IsDots() const;
    // Whether the found file has one of the attribute bits `mask`.
    BOOL MatchesMask(unsigned long mask) const;
    void Close();

private:
    std::uint8_t data_[0x1C - 0];
};
static_assert(sizeof(CFileFind) == 0x1C);

// The virtual slots of MFC 4.2's CFileFind for callers that dispatch through it; slots declared `void
// Name()` keep their place only (8-13 are the six GetLast*Time / GetCreationTime overloads).
class __declspec(novtable) CFileFindVirtuals {
public:
    virtual void GetRuntimeClass();
    virtual void DeletingDestructor();
    virtual void Serialize();
    virtual CString GetFileName() const;
    virtual CString GetFilePath() const;
    virtual CString GetFileTitle() const;
    virtual void GetFileURL();
    virtual void GetRoot();
    virtual void GetTime8();
    virtual void GetTime9();
    virtual void GetTime10();
    virtual void GetTime11();
    virtual void GetTime12();
    virtual void GetTime13();
    virtual BOOL MatchesMask(unsigned long mask) const;     // file attribute bits
    virtual BOOL IsDots() const;
    virtual BOOL FindFile(const char* pattern, unsigned long flags);
    virtual BOOL FindNextFile();
    virtual BOOL CloseContext();
};

// CTime (a time_t). No constructor, so it can be a plain local: Construct is the constructor.
class CTime {
public:
    // CTime(year, month, day, hour, minute, second, dst) through mktime.
    CTime* Construct(int year, int month, int day, int hour, int minute, int second, int dst);

    // Strftime of the local time into at most 127 characters (empty when it does not fit).
    CString Format(const char* format) const;

    long time_;
};
static_assert(sizeof(CTime) == 4);

// CPlex: the block allocator behind MFC's lists and maps.
struct CPlex {
    CPlex* next;
};
namespace impl {
CPlex* __stdcall CreatePlex(CPlex*& head, unsigned count, unsigned elementSize);
int __fastcall FreePlexChain(CPlex* head);
} // namespace impl
inline CPlex* PlexCreate(CPlex*& head, unsigned count, unsigned elementSize) {
    return impl::CreatePlex(head, count, elementSize);
}
inline void PlexFreeDataChain(CPlex* head) { impl::FreePlexChain(head); }

// ---- Collection templates (AFXTEMPL.H) ------------------------------------------------------------
// The game's CArray / CList / CTypedPtrList instantiations share one layout each and differ only in the
// element type and the vtable, so one template serves all of them (docs/notes/core.md, "Shared containers").
// The vtable is a plain field: an object game.exe constructs keeps the one its constructor wrote, and code
// that builds one itself passes the instantiation's vtable. The templates have no constructors, so they can
// sit in layouts and be brace-initialised. The member functions are MFC 4.2's, written once.

// MFC 4.2's default ConstructElements / DestructElements for element types without constructors: new
// elements are zero-filled, nothing is destroyed. An instantiation whose elements are constructed out of
// line (CString, records with constructors) passes its own policy with the same two static functions.
template <typename T>
struct PlainElements {
    static void Construct(T* elements, int count) {
        std::memset(static_cast<void*>(elements), 0, static_cast<std::size_t>(count) * sizeof(T));
    }
    static void Destruct(T*, int) {}
};

// CString elements: MFC 4.2's afxtempl.h specialises ConstructElements / DestructElements for CString: each
// new element the empty string, each removed one released. CArray<CString> takes them by default.
template <>
struct PlainElements<CString> {
    static void Construct(CString* elements, int count) {
        for (; count--; ++elements) ::new (static_cast<void*>(elements)) CString;
    }
    static void Destruct(CString* elements, int count) {
        for (; count--; ++elements) elements->~CString();
    }
};

// No construction at all: the engine's own arrays and the instantiations the game compiled without
// the zeroing (new slots keep the heap's bytes).
template <typename T>
struct RawElements {
    static void Construct(T*, int) {}
    static void Destruct(T*, int) {}
};

// An element policy's Relocate for elements that are not relocatable as bytes (a std::string member): each
// element move-constructed at `to` and its source destroyed, in the direction the overlap needs.
template <typename T>
void MoveElements(T* to, T* from, int count) {
    auto move = [](T* target, T* source) {
        std::construct_at(target, std::move(*source));
        std::destroy_at(source);
    };
    if (to < from) {
        for (int i = 0; i < count; ++i) move(to + i, from + i);
    } else {
        for (int i = count; i-- > 0;) move(to + i, from + i);
    }
}

// Only what the CArray serialisers use: the mode (bit 0: loading).
class CArchive {
public:
    bool IsLoading() const { return (mode_ & 1) != 0; }
    unsigned Read(void* buffer, unsigned bytes);
    void Write(const void* buffer, unsigned bytes);
    unsigned long ReadCount();
    void WriteCount(unsigned long count);
    void Flush();
    CFile* File() const { return file_; }

private:
    std::uint8_t unused_00[0x14];
    unsigned mode_;
    std::uint8_t unused_18[8];
    CFile* file_;
};

template <typename T, typename Elements = PlainElements<T>>
struct CArray {
    void* vtable;
    T* data;        // operator new storage (new BYTE[])
    int size;
    int maxSize;
    int growBy;     // 0 = size / 8 clamped to 4..1024

    T& operator[](int index) { return data[index]; }
    const T& operator[](int index) const { return data[index]; }
    int GetSize() const { return size; }

    // The constructor body: the instantiation's vtable, empty (like CByteArray's).
    CArray* Construct(const void* vtablePointer) {
        vtable = const_cast<void*>(vtablePointer);
        data = nullptr;
        growBy = 0;
        maxSize = 0;
        size = 0;
        return this;
    }

    // `from` and `to` may overlap; the slots at `to` hold no live elements. The element policy's Relocate where an
    // element is not relocatable as bytes (a std::string, whose debug build tracks its own address).
    static void Relocate(T* to, T* from, int count) {
        if constexpr (requires { Elements::Relocate(to, from, count); }) {
            Elements::Relocate(to, from, count);
        } else {
            std::memmove(static_cast<void*>(to), from, static_cast<std::size_t>(count) * sizeof(T));
        }
    }

    // 0 frees the storage; growing past maxSize reallocates to max(newSize, maxSize + step), relocating the old
    // elements and constructing only the new ones; the old storage is freed without destroying anything.
    // newGrowBy -1 keeps the step.
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
            data = gamecrt::operator_new_array<T>(static_cast<std::size_t>(newSize));
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
            T* fresh = gamecrt::operator_new_array<T>(static_cast<std::size_t>(newMax));
            Relocate(fresh, data, size);
            Elements::Construct(fresh + size, newSize - size);
            gamecrt::operator_delete(data);
            data = fresh;
            size = newSize;
            maxSize = newMax;
        }
    }

    void RemoveAll() { SetSize(0, -1); }

    // Serialize (CObject::Serialize is empty): the count (ReadCount then SetSize when loading), then the
    // elements as raw bytes (MFC's default SerializeElements). `Archive` is the facade's CArchive or mfc::impl's.
    template <typename Archive>
    void Serialize(Archive* ar) {
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

    int Add(const T& value) {
        int index = size;
        SetAtGrow(index, value);
        return index;
    }

    T& ElementAt(int index) { return data[index]; }

    // GetAt(nIndex) returning the element by value (the result pointer first; returns it).
    T* GetAt(T* result, int index) const {
        *result = data[index];
        return result;
    }

    // InsertAt: grows by `count`, moves the tail up (Relocate; the slots it vacates are
    // destroyed and constructed again), then assigns `value` to each new slot.
    void InsertAt(int index, const T& value, int count = 1) {
        if (index >= size) {
            SetSize(index + count, -1);
        } else {
            int oldSize = size;
            SetSize(size + count, -1);
            Elements::Destruct(data + oldSize, count);
            Relocate(data + index + count, data + index, oldSize - index);
            Elements::Construct(data + index, count);
        }
        while (count--) data[index++] = value;
    }

    // RemoveAt: destroys `count` elements and moves the tail down (Relocate); the storage stays.
    void RemoveAt(int index, int count = 1) {
        int moveCount = size - (index + count);
        Elements::Destruct(data + index, count);
        if (moveCount) Relocate(data + index, data + index + count, moveCount);
        size -= count;
    }

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
    Node* head;
    Node* tail;
    int count;
    Node* free;
    CPlex* blocks;
    int blockSize;

    int GetCount() const { return count; }
    bool IsEmpty() const { return count == 0; }
    // GetNext: the element at `position`, the position advanced (the game's out-of-line copies
    T& GetNext(Node*& position) {
        Node* node = position;
        position = node->next;
        return node->data;
    }

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

    Node* FindIndex(int index) const {
        if (index >= count || index < 0) return nullptr;
        Node* n = head;
        while (index--) n = n->next;
        return n;
    }
};
static_assert(sizeof(CList<int>) == 0x1C);

// CMap<K, ARG_K, V, ARG_V> (0x1C bytes): vtable, hash table, table size, count, free associations, CPlex
// blocks, block size. An association is next, hash, key, value (dword packed, so a 64-bit key sits at +8).
// Hashing and the association pool stay with each instantiation's own GetAssocAt / InitHashTable /
// NewAssoc; the walk and RemoveAll, which the game inlines, are written here.
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
    Assoc** table;
    std::uint32_t hashSize; // (17 until InitHashTable)
    int count;
    Assoc* freeList;
    CPlex* blocks;
    int blockSize;

    // GetNextAssoc as inlined: the association after `assoc` (the next in its chain, else the head
    // of a later bucket), or null; null starts at the first bucket.
    Assoc* Next(const Assoc* assoc) const {
        if (assoc && assoc->next) return assoc->next;
        for (std::uint32_t b = assoc ? assoc->hash + 1 : 0; b < hashSize; ++b)
            if (table[b]) return table[b];
        return nullptr;
    }

    // InitHashTable(nHashSize, bAllocNow): the old table deleted; a zeroed one of
    // `newHashSize` buckets when `allocNow`; the size kept either way.
    void InitHashTable(std::uint32_t newHashSize, int allocNow) {
        if (table) {
            gamecrt::operator_delete(table);
            table = nullptr;
        }
        if (allocNow) {
            table = gamecrt::operator_new_array<Assoc*>(newHashSize);
            std::memset(table, 0, newHashSize * sizeof(Assoc*));
        }
        hashSize = newHashSize;
    }

    // GetAssocAt(key, nHash&): the key's bucket (the key, its low dword for a 64-bit one,
    // shifted right by 4) into `hashOut`; its association, or null (also when there is no table).
    Assoc* GetAssocAt(const K& key, std::uint32_t& hashOut) const {
        hashOut = (static_cast<std::uint32_t>(key) >> 4) % hashSize;
        if (!table) return nullptr;
        for (Assoc* assoc = table[hashOut]; assoc; assoc = assoc->next)
            if (assoc->key == key) return assoc;
        return nullptr;
    }

    // NewAssoc: an association from the free list (a new block of `blockSize` when it is empty), the count
    // raised, key and value zeroed. `next` and `hash` are the caller's.
    Assoc* NewAssoc() {
        if (!freeList) {
            CPlex* block = PlexCreate(blocks, static_cast<unsigned>(blockSize), sizeof(Assoc));
            Assoc* fresh = reinterpret_cast<Assoc*>(block + 1) + (blockSize - 1);
            for (int i = blockSize - 1; i >= 0; --i, --fresh) {
                fresh->next = freeList;
                freeList = fresh;
            }
        }
        Assoc* assoc = freeList;
        freeList = assoc->next;
        ++count;
        std::memset(static_cast<void*>(&assoc->key), 0, sizeof(K));
        std::memset(static_cast<void*>(&assoc->value), 0, sizeof(V));
        return assoc;
    }

    // operator[](key): the value of `key`, a new association (value zero) put at the head of
    // its bucket when missing (the table allocated first if there is none).
    V& ElementAt(const K& key) {
        std::uint32_t hash;
        Assoc* assoc = GetAssocAt(key, hash);
        if (!assoc) {
            if (!table) InitHashTable(hashSize, 1);
            assoc = NewAssoc();
            assoc->hash = hash;
            assoc->key = key;
            assoc->next = table[hash];
            table[hash] = assoc;
        }
        return assoc->value;
    }

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

// CStringArray (afxcoll): CArray's layout over CStrings. No constructor, so it can sit in layouts: Construct /
// Destruct are the constructor and destructor.
struct CStringArray {
    void* vtable;       // written by Construct
    CString* data;
    int size;
    int maxSize;
    int growBy;

    void Construct();
    void Destruct();
    void SetSize(int newSize, int newGrowBy = -1);
    void SetAtGrow(int index, const CString& value);
    void SetAtGrow(int index, const char* text);
    void RemoveAt(int index, int count = 1);
    void Copy(const CStringArray& source);
    int Append(const CStringArray& source);             // the old size
    void InsertAt(int index, const CString& value, int count = 1);

    int GetSize() const { return size; }
    CString& operator[](int index) { return data[index]; }
    const CString& operator[](int index) const { return data[index]; }
};
static_assert(sizeof(CStringArray) == 0x14);

// CTypedPtrList<CPtrList, T*>: a CPtrList (the list of void*, same layout) with typed elements. The game
// inlines its operations; they are CList<T*>'s (a new node's data is zeroed, nothing destroyed).
template <typename T>
using CTypedPtrList = CList<T*>;


CString __stdcall operator+(const CString& a, const char* b);
CString __stdcall operator+(const char* a, const CString& b);
CString __stdcall operator+(const CString& a, const CString& b);
CString __stdcall operator+(const CString& a, char b);
CString __stdcall operator+(char a, const CString& b);
// The same bridge: text joined with a std::string, as the std::string side's type.
inline std::string operator+(const std::string& a, const CString& b) { return a + b.GetString(); }
inline CString operator+(const CString& a, const std::string& b) { return a + b.c_str(); }

} // namespace mfc
