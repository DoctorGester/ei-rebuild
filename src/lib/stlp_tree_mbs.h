#pragma once

// The key order of the exe's maps that had MFC CString keys (stlp_tree.h's map third argument): less<CString>,
// _mbscmp below zero with the node's key first, on the text up to its first zero byte. Defined in stlp_tree.cpp,
// so this header does not bring in windows.h.

#include <string>

namespace stlp {

struct MbsLess {
    static bool Less(const std::string& a, const std::string& b);
};

} // namespace stlp
