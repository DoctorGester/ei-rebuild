// The game's STLport deque<T> instantiations (stlp_deque.h): nothing reaches them by game.exe's address
// (tools/registration_readers.py, C20c), so none is registered; our code uses stlp::deque directly. Two
// instantiations: the camera's view stack (0x24-byte entries, 0x1F8-byte nodes; 0041xxxx) and the material's
// entries (0x10-byte, 0x200-byte nodes; 004A / 0051 / 0068xxxx), the node size read from each one's
// _M_create_nodes and push_back_aux.
#include "stlp_deque.h"

namespace {

using ViewStack = stlp::DequeAbi<stlp::Opaque<0x24>>;
using MaterialEntries = stlp::DequeAbi<stlp::Opaque<0x10>>;
static_assert(sizeof(ViewStack::Deque) == 0x28 && ViewStack::Deque::kNodeBytes == 0x1F8);
static_assert(MaterialEntries::Deque::kNodeBytes == 0x200);

} // namespace

