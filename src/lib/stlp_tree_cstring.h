#pragma once

// The key order of the exe's maps with MFC CString keys (stlp_tree.h's map third argument): less<
// CString>, _mbscmp below zero with the node's key first (e.g. 005FAF30, 005E11A0). Defined in
// stlp_tree.cpp, so this header does not bring in windows.h.

namespace mfc {
class CString;
}

namespace stlp {

struct CStringLess {
    static bool Less(const mfc::CString& a, const mfc::CString& b);
};

} // namespace stlp
