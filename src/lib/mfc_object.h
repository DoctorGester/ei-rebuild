#pragma once

// MFC 4.2's CObject as the game's copy has it: the three slots every MFC object starts with, 0 GetRuntimeClass,
// 1 the (scalar deleting) destructor, 2 Serialize (CObject's is empty). The library classes that keep the
// exe's tables derive from it (the exceptions, CWnd). On the game's heap: null without memory, and then no
// constructor runs.

#include "crt.h"

#include <cstddef>
#include <new>

namespace mfc::impl {

struct CRuntimeClass;

class CObject {
public:
    virtual const CRuntimeClass* GetRuntimeClass() const = 0;   // 0: the class's record (mfc_runtime_class.h)
    virtual ~CObject() = default;                               // 1
    virtual void Serialize(void*) {}

    static void* operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block) { gamecrt::operator_delete(block); }
    static void operator delete(void*, void*) noexcept {}

protected:
    CObject() = default;
};

} // namespace mfc::impl
