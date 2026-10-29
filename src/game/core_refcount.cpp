#include "core_refcount.h"

void CRefObject::Shutdown() {
    ++refs;
    OnShutdown();
    --refs;
}
