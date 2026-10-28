#include "mfc.h"
#include "mfc_time.h"

namespace mfc {

namespace impl {

CPlex* CreatePlex(CPlex*& head, unsigned count, unsigned elementSize) {
    auto* block = gamecrt::operator_new_bytes_as<CPlex>(sizeof(CPlex) + count * elementSize);
    block->next = head;
    head = block;
    return block;
}

int FreePlexChain(CPlex* head) {
    while (head) {
        CPlex* next = head->next;
        gamecrt::operator_delete(head);
        head = next;
    }
    return 0;
}

} // namespace impl

#ifndef EI_VECTORS_REPLAY   // ei_vectors does not link mfc_time.cpp
CTime* CTime::Construct(int year, int month, int day, int hour, int minute, int second, int dst) {
    reinterpret_cast<impl::CTime*>(this)->Construct(year, month, day, hour, minute, second, dst);
    return this;
}
std::string CTime::Format(const char* format) const { return reinterpret_cast<const impl::CTime*>(this)->Format(format); }
#endif

} // namespace mfc
