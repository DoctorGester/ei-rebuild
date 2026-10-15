#pragma once

// Helpers for the destructors and copies the compiler wrote for game records (S13): STLport
// vectors destroyed member by member, and CStrings that records declare as plain char pointers.

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

namespace records {

// An STLport vector's destructor: its elements destroyed in order, then the storage returned to
// the node pool (operator delete above 128 bytes).
template <typename T>
void DestroyVector(stlp::vector<T>& vector) {
    for (T* p = vector.begin(); p != vector.end(); ++p) p->~T();
    vector.FreeStorage();
}

// A CString member declared as its data pointer.
inline mfc::CString& AsCString(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }
inline mfc::CString& AsCString(const char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }
inline const mfc::CString& AsCString(char* const& field) { return *reinterpret_cast<const mfc::CString*>(&field); }
inline const mfc::CString& AsCString(const char* const& field) {
    return *reinterpret_cast<const mfc::CString*>(&field);
}

// 005AE5B0 / 005AE5D0 (RET 4): the deleting destructors of the figure maps' pairs, whose only
// member with a destructor is the leading string key (bodies 005AD4D0 / 004FAF10: its storage back
// to the node pool). Returns the pair.
inline stlp::string* __fastcall LeadingStringDelete(stlp::string* pair, void*, unsigned flags) {
    pair->~string();
    if (flags & 1) gamecrt::operator_delete(pair);
    return pair;
}

} // namespace records
