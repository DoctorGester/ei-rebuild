// Out-of-line list<T> members our code calls (stlp_list.h, stlp_list_refs.h). The game's list instantiations
// need no registrations at their addresses: nothing reaches them by game.exe's address (tools/registration_readers.py,
// C20c); our code uses stlp::list directly.
#include "stlp_list.h"

#include "stlp_list_refs.h"
#include "mfc.h"

namespace stlp {

void ListRemoveHeld(list<CEngineObject*>* self, CEngineObject* const& value) {
    self->Remove<HeldObject>(value, [](CEngineObject* a, CEngineObject* b) { return a == b; });
}

void ListRemoveInt(list<int>* self, const int& value) {
    self->Remove(value, [](int a, int b) { return a == b; });
}

} // namespace stlp

