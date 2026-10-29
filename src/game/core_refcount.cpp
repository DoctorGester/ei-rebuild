#include "core_refcount.h"

void RefObject::Shutdown() {
    ++refs;
    OnShutdown();
    --refs;
}
