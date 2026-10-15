// The game's STLport deque<T> instantiations (stlp_deque.h), registered at their own addresses so
// the standalone image has them; camera.h's forwarders keep calling these addresses. Checked by
// tests/verify/verify_stlp_deque.cpp. Two instantiations: the camera's view stack (0x24-byte
// entries, 0x1F8-byte nodes; 0041xxxx) and the material's entries (0x10-byte, 0x200-byte nodes;
// 004A / 0051 / 0068xxxx), the node size read from each one's _M_create_nodes and push_back_aux.
#include "stlp_deque.h"

#include "ei/hook.h"

namespace stlp {

AnyDequeIterator* __fastcall DequeIteratorCopy(AnyDequeIterator* self, void*, const AnyDequeIterator* from) {
    *self = *from;
    return self;
}

void __stdcall DequeDestroyRange(AnyDequeIterator, AnyDequeIterator) {}

void __stdcall DequeDestroyRangeAux(AnyDequeIterator, AnyDequeIterator, const void*) {}

} // namespace stlp

namespace {

using ViewStack = stlp::DequeAbi<stlp::Opaque<0x24>>;
using MaterialEntries = stlp::DequeAbi<stlp::Opaque<0x10>>;
static_assert(sizeof(ViewStack::Deque) == 0x28 && ViewStack::Deque::kNodeBytes == 0x1F8);
static_assert(MaterialEntries::Deque::kNodeBytes == 0x200);

} // namespace

// The camera's view stack.
EI_REPLACE(0x00411AB0, ViewStack::Construct);
EI_REPLACE(0x00411C00, ViewStack::InitializeMap);
EI_REPLACE(0x00411EE0, ViewStack::CreateNodes);
EI_REPLACE(0x0040CFB0, ViewStack::Destruct);
EI_REPLACE(0x00411D60, ViewStack::PushBackAux);
EI_REPLACE(0x00411F70, ViewStack::ReallocateMap);
EI_REPLACE(0x00411810, stlp::DequeIteratorCopy);
EI_REPLACE(0x00411DF0, stlp::DequeDestroyRange);
EI_REPLACE(0x00412140, stlp::DequeDestroyRange);
EI_REPLACE(0x00412190, stlp::DequeDestroyRangeAux);

// The material's entries.
EI_REPLACE(0x0068E0B0, MaterialEntries::Construct);
EI_REPLACE(0x0068E1C0, MaterialEntries::InitializeMap);
EI_REPLACE(0x0068E320, MaterialEntries::CreateNodes);
EI_REPLACE(0x0068DD40, MaterialEntries::Destruct);
EI_REPLACE(0x0051E840, MaterialEntries::PushBackAux);
EI_REPLACE(0x004A39C0, MaterialEntries::ReallocateMap);
EI_REPLACE(0x004A2BF0, stlp::DequeIteratorCopy);
EI_REPLACE(0x004A2FC0, stlp::DequeDestroyRange);
EI_REPLACE(0x004A3060, stlp::DequeDestroyRange);
EI_REPLACE(0x004A3090, stlp::DequeDestroyRangeAux);
