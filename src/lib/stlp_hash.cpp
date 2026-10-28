// The game's STLport hashtable instantiations (stlp_hash.h); our code calls them by name.
// Instantiations with the same instructions (addresses masked,
// node sizes and free lists kept) share one body; each node size was read from the instantiation's
// _M_new_node or from the free list its clear() pushes to.
#include "stlp_hash.h"

#include <cstdint>
#include <cstring>

namespace stlp {

std::uint32_t __fastcall HashString(const std::string* s) { return HashChars(s->data(), s->data() + s->size()); }

} // namespace stlp
