#include "stlp.h"

#include "crt.h"

#include <cstddef>

namespace stlp {

void* allocate(std::size_t bytes) { return bytes ? libcrt::NodeAllocate(bytes) : nullptr; }
void deallocate(void* p, std::size_t bytes) { if (p) libcrt::NodeDeallocate(p, bytes); }

} // namespace stlp
