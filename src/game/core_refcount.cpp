#include "game/core_refcount.h"

#include "lib/crt.h"

RefObject* RefObject::Clone() {
    game::FatalExit("%s", "Cloning an object that is never shared twice");
}

void RefObject::Shutdown() {
    ++refs;
    OnShutdown();
    --refs;
}
