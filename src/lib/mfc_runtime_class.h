#pragma once

// MFC 4.2's CRuntimeClass records of the game's copy, as named data. See docs/notes/standalone.md, "The
// image's source files".

#include "mfc_gdi.h"
#include "mfc_handle_map.h"

namespace mfc::impl {

// The create functions of the records here (the temporary DC and GDI object ones are mfc_gdi.h's): a new object
// of the class from operator new (or its fixed allocator), constructed; null when out of memory.
void* __stdcall CreateCGdiObject();     // CGdiObject: no handle (mfc_gdi.cpp)
void* __stdcall CreateCDC();            // CDC (mfc_gdi.cpp)
void* __stdcall CreateCWnd();           // CWnd: operator new, the default constructor

inline constexpr CRuntimeClass kClassCObject = /*00745D20*/ {"CObject", 0x4, 0xFFFF, nullptr, nullptr, nullptr};
inline constexpr CRuntimeClass kClassCException = /*007465D0*/ {"CException", 0x8, 0xFFFF, nullptr, &kClassCObject, nullptr};

inline constexpr CRuntimeClass kClassCGdiObject = /*00746098*/ {"CGdiObject", 0x8, 0xFFFF, &CreateCGdiObject, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCBrush = /*00746058*/ {"CBrush", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCDC = /*00746138*/ {"CDC", 0x10, 0xFFFF, &CreateCDC, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCCmdTarget = /*007468A8*/ {"CCmdTarget", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCFont = /*00746038*/ {"CFont", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCMapPtrToPtr = /*00746870*/ {"CMapPtrToPtr", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCMemoryException = /*007465A0*/ {"CMemoryException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCPen = /*00746078*/ {"CPen", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCResourceException = /*00746180*/ {"CResourceException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCTempDC = /*00745FB0*/ {"CTempDC", 0x10, 0xFFFF, &CreateTempDC, &kClassCDC, nullptr};
inline constexpr CRuntimeClass kClassCTempGdiObject = /*00745F88*/ {"CTempGdiObject", 0x8, 0xFFFF, &CreateTempGdiObject, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCWnd = /*007469D8*/ {"CWnd", 0x3c, 0xFFFF, &CreateCWnd, &kClassCCmdTarget, nullptr};
inline constexpr CRuntimeClass kClassCUserException = /*00746158*/ {"CUserException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCWinThread = /*007466B8*/ {"CWinThread", 0x68, 0xFFFF, nullptr, &kClassCCmdTarget, nullptr};

} // namespace mfc::impl
