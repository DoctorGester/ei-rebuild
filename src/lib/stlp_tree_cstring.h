#pragma once

// The key order of the exe's maps with MFC CString keys (stlp_tree.h's map third argument): less<
// CString>, _mbscmp below zero with the node's key first. MbsLess is the same order on std::string keys
// (the text up to its first zero byte, as the CString held it). Defined in stlp_tree.cpp, so this header
// does not bring in windows.h.

#include <string>

namespace mfc {
class CString;
}

namespace stlp {

struct CStringLess {
    static bool Less(const mfc::CString& a, const mfc::CString& b);
};

struct MbsLess {
    static bool Less(const std::string& a, const std::string& b);
};

} // namespace stlp
