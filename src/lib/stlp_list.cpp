// Out-of-line list<T> members our code calls (stlp_list.h, stlp_list_refs.h). The game's list instantiations
// need no registrations at their addresses: nothing reaches them by game.exe's address (tools/registration_readers.py,
// C20c); our code uses stlp::list directly.
#include "stlp_list.h"

#include "stlp_list_refs.h"
#include "mfc.h"

namespace stlp {

void __fastcall ListRemoveHeld(list<CEngineObject*>* self, void*, CEngineObject* const* value) {
    self->Remove<HeldObject>(*value, [](CEngineObject* a, CEngineObject* b) { return a == b; });
}

void __fastcall ListRemoveInt(list<int>* self, void*, const int* value) {
    self->Remove(*value, [](int a, int b) { return a == b; });
}

char __stdcall ListForEachRelease(list<CEngineObject*>::Node* first, list<CEngineObject*>::Node* last, char f) {
    for (; first != last; first = first->next) HeldObject::Destroy(&first->data);
    return f;
}

} // namespace stlp

