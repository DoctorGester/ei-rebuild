#pragma once

// Helpers for the destructors and copies the compiler wrote for game records (S13): STLport
// vectors destroyed member by member, and the figure maps' pairs.

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

// 005AE5B0 / 005AE5D0 (RET 4): the deleting destructors of the figure maps' pairs, whose only
// member with a destructor is the leading string key (bodies 005AD4D0 / 004FAF10: its storage back
// to the node pool): mfc::impl::ScalarDeleting<&DestructLeadingString>::Delete. Returns the pair.
inline void __fastcall DestructLeadingString(stlp::string* pair) { pair->~string(); }

} // namespace records
