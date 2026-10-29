#include "core_refcount.h"

void CRefObject::Shutdown() {
    ++refs_;
    OnShutdown();
    --refs_;
}
