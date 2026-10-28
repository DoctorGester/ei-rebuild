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

// CString: game.exe's CString in the layouts the function vectors read and replay (tests/vectors' Original* views of
// game.exe's objects): a single pointer to the character data; the reference count, length and capacity live in a
// header just before it. An empty string points at the shared afxEmptyString data. The game's text is std::string
// (str.h), src/lib's MFC too; only ei_vectors defines these members (tests/vectors/replay_mfc.cpp).
class CString {
public:
    CString();
    CString(const char* s);
    CString(const char* s, int length);
    CString(const CString& s);
    ~CString();
    const CString& operator=(const char* s);
    const CString& operator=(const CString& s);

    int GetLength() const { return reinterpret_cast<const int*>(data_)[-2]; }
    operator const char*() const { return data_; }

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

// CTime (a time_t). No constructor, so it can be a plain local: Construct is the constructor.
class CTime {
public:
    // CTime(year, month, day, hour, minute, second, dst) through mktime.
    CTime* Construct(int year, int month, int day, int hour, int minute, int second, int dst);

    // Strftime of the local time into at most 127 characters (empty when it does not fit).
    std::string Format(const char* format) const;

    long time_;
};
static_assert(sizeof(CTime) == 4);

// CPlex: the block allocator behind MFC's maps and fixed allocators (mfc_handle_map.h, mfc_gdi.h).
struct CPlex {
    CPlex* next;
};
namespace impl {
// CPlex::Create: a block of `count` elements of `elementSize` bytes after the CPlex header (operator new), linked in
// front of `head`. Returns the block.
CPlex* CreatePlex(CPlex*& head, unsigned count, unsigned elementSize);
// CPlex::FreeDataChain (`head` may be null): every block of the chain freed. Returns 0.
int FreePlexChain(CPlex* head);
} // namespace impl
inline CPlex* PlexCreate(CPlex*& head, unsigned count, unsigned elementSize) {
    return impl::CreatePlex(head, count, elementSize);
}
inline void PlexFreeDataChain(CPlex* head) { impl::FreePlexChain(head); }

} // namespace mfc
