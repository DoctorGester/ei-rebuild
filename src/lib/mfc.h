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

} // namespace mfc
