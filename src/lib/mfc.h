#pragma once

// MFC 4.2 classes statically linked into game.exe, used by rewritten code through the original
// implementations (config/library.csv). Layouts match MFC so objects can be shared with original code.

#include <cstdint>

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

private:
    void* vtable_;
    void* handle_;
    BOOL closeOnDelete_;
    char* fileName_;    // CString; raw here so the naked forwarders have no members to construct
};
static_assert(sizeof(CFile) == 0x10);

// CFileFind (0x1C bytes).
class CFileFind {
public:
    CFileFind();                                                    // 006E6370
    ~CFileFind();                                                   // 006E63AD
    BOOL FindFile(const char* pattern, unsigned long flags);       // 006E6433

private:
    std::uint8_t data_[0x1C - 0];
};
static_assert(sizeof(CFileFind) == 0x1C);

// CPlex: the block allocator behind MFC's lists and maps.
struct CPlex {
    CPlex* next;
    // followed by the block's elements
};
CPlex* __stdcall PlexCreate(CPlex*& head, unsigned count, unsigned elementSize);   // 006FE8EF
void __fastcall PlexFreeDataChain(CPlex* head);                                    // 006FE90F

// CTypedPtrList<CPtrList, T*> (0x1C bytes). The game inlines these operations; this mirrors MFC 4.2.
template <typename T>
struct CTypedPtrList {
    struct Node {
        Node* next;
        Node* prev;
        T* data;
    };

    void* vtable;
    Node* head;
    Node* tail;
    int count;
    Node* free;
    CPlex* blocks;
    int blockSize;

    // NewNode: take a node from the free list, carving a new block of `blockSize` when empty.
    Node* NewNode(Node* prev, Node* next) {
        if (!free) {
            CPlex* block = PlexCreate(blocks, blockSize, sizeof(Node));
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
        node->data = nullptr;
        return node;
    }

    // FreeNode: return to the free list; the last node frees all blocks (RemoveAll).
    void FreeNode(Node* node) {
        node->next = free;
        free = node;
        if (--count == 0) {
            count = 0;
            free = nullptr;
            tail = nullptr;
            head = nullptr;
            PlexFreeDataChain(blocks);
            blocks = nullptr;
        }
    }

    Node* AddHead(T* value) {
        Node* node = NewNode(nullptr, head);
        node->data = value;
        if (head) head->prev = node;
        else tail = node;
        head = node;
        return node;
    }

    void RemoveAll() {
        count = 0;
        free = nullptr;
        tail = nullptr;
        head = nullptr;
        PlexFreeDataChain(blocks);
        blocks = nullptr;
    }

    Node* AddTail(T* value) {
        Node* node = NewNode(tail, nullptr);
        node->data = value;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        return node;
    }

    Node* Find(const T* value) const {
        for (Node* n = head; n; n = n->next)
            if (n->data == value) return n;
        return nullptr;
    }

    void RemoveAt(Node* node) {
        if (node == head) head = node->next;
        else node->prev->next = node->next;
        if (node == tail) tail = node->prev;
        else node->next->prev = node->prev;
        FreeNode(node);
    }
};

CString __stdcall operator+(const CString& a, const char* b);   // 00700C05
CString __stdcall operator+(const char* a, const CString& b);   // 00700C79
CString __stdcall operator+(const CString& a, const CString& b); // 00700B9F

} // namespace mfc
