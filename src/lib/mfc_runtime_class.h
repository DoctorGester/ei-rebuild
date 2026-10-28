#pragma once

// MFC 4.2's CRuntimeClass records of the game's copy, as named data. See docs/notes/standalone.md, "The
// image's source files".

#include "mfc_object.h"

namespace mfc::impl {

// CRuntimeClass: a class's record.
struct CRuntimeClass {
    const char* className;
    int objectSize;
    unsigned schema;                // 0xFFFF: not serializable
    void*(__stdcall* create)();     // null: not creatable
    const CRuntimeClass* baseClass;
    CRuntimeClass* nextClass;       // the module state's class list; AfxClassInit links it
};

// The create function of CWnd's record: operator new, the default constructor; null when out of memory.
void* __stdcall CreateCWnd();

inline constexpr CRuntimeClass kClassCObject = /*00745D20*/ {"CObject", 0x4, 0xFFFF, nullptr, nullptr, nullptr};
inline constexpr CRuntimeClass kClassCException = /*007465D0*/ {"CException", 0x8, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCCmdTarget = /*007468A8*/ {"CCmdTarget", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCMemoryException = /*007465A0*/ {"CMemoryException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCResourceException = /*00746180*/ {"CResourceException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCWnd = /*007469D8*/ {"CWnd", 0x3c, 0xFFFF, &CreateCWnd, &kClassCCmdTarget, nullptr};
inline constexpr CRuntimeClass kClassCUserException = /*00746158*/ {"CUserException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};

} // namespace mfc::impl
