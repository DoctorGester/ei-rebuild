#pragma once

// MFC 4.2's CRuntimeClass records of the game's copy (C18e), as named data: the 41 the exe holds, 36 here (const, in
// .rdata) and the 5 serializable ones (CByteArray, CDWordArray, CStringArray, CWordArray in mfc_coll.cpp, CDib in
// game/dib.cpp) in .data, where the static initialisers link them into the module state's class list
// (AfxClassInit writes m_pNextClass). Each record names its base's record and its create function (ours,
// registered at the original's address: the pointers are compared by tools/named_data.py, "Pointer elements"). The
// hierarchy is one piece: a record's base is a named range, and game.exe's data holds no pointer into a named
// range, so CObject's record (the root) could be named only once every record that points at it was.
// Only the exceptions', files' and GDI temporaries' records are read (GetRuntimeClass, the handle maps); the rest
// are what their base pointers and create functions reach.

#include "mfc_gdi.h"
#include "mfc_handle_map.h"

namespace mfc::impl {

// The create functions of the records here (the temporary DC and GDI object ones are mfc_gdi.h's): a new object
// of the class from operator new (or its fixed allocator), constructed; null when out of memory.
void* __stdcall CreateCGdiObject();     // 0070BF93 CGdiObject: vtable 007463D4, no handle (mfc_gdi.cpp)
void* __stdcall CreateCDC();            // 0070BF64 CDC (mfc_gdi.cpp)
void* __stdcall CreateCMenu();          // 0070DC80 CMenu: vtable 007471FC, no handle (mfc_window.cpp)
void* __stdcall CreateCTempMenu();      // 0070DCA0 CTempMenu: from its fixed allocator, vtable 00747214
void* __stdcall CreateCWnd();           // 0070D32B CWnd: operator new, the default constructor
void* __stdcall CreateCTempWnd();       // 0070D363 CTempWnd: its fixed allocator, CWnd's constructor, vtable 00746EC4

inline constexpr CRuntimeClass kClassCObject = /*00745D20*/ {"CObject", 0x4, 0xFFFF, nullptr, nullptr, nullptr};
inline constexpr CRuntimeClass kClassCException = /*007465D0*/ {"CException", 0x8, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCArchiveException = /*007467D8*/ {"CArchiveException", 0x10, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCGdiObject = /*00746098*/ {"CGdiObject", 0x8, 0xFFFF, &CreateCGdiObject, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCBitmap = /*00746018*/ {"CBitmap", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCBrush = /*00746058*/ {"CBrush", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCDC = /*00746138*/ {"CDC", 0x10, 0xFFFF, &CreateCDC, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCClientDC = /*00746110*/ {"CClientDC", 0x14, 0xFFFF, nullptr, &kClassCDC, nullptr};
inline constexpr CRuntimeClass kClassCCmdTarget = /*007468A8*/ {"CCmdTarget", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCFile = /*00745DB0*/ {"CFile", 0x10, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCFileException = /*00746820*/ {"CFileException", 0x14, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCFileFind = /*00745EC0*/ {"CFileFind", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCFont = /*00746038*/ {"CFont", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCMapPtrToPtr = /*00746870*/ {"CMapPtrToPtr", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCMemFile = /*00746628*/ {"CMemFile", 0x28, 0xFFFF, nullptr, &kClassCFile, nullptr};
inline constexpr CRuntimeClass kClassCMemoryException = /*007465A0*/ {"CMemoryException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCMenu = /*007471D8*/ {"CMenu", 0x8, 0xFFFF, &CreateCMenu, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCNotSupportedException = /*00746570*/ {"CNotSupportedException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCPaintDC = /*007460C0*/ {"CPaintDC", 0x54, 0xFFFF, nullptr, &kClassCDC, nullptr};
inline constexpr CRuntimeClass kClassCPalette = /*00745FF0*/ {"CPalette", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCPen = /*00746078*/ {"CPen", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCPtrArray = /*00746F88*/ {"CPtrArray", 0x14, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCPtrList = /*00746FC0*/ {"CPtrList", 0x1c, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCResourceException = /*00746180*/ {"CResourceException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCRgn = /*00745FD0*/ {"CRgn", 0x8, 0xFFFF, nullptr, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCStdioFile = /*007464C8*/ {"CStdioFile", 0x14, 0xFFFF, nullptr, &kClassCFile, nullptr};
inline constexpr CRuntimeClass kClassCTempDC = /*00745FB0*/ {"CTempDC", 0x10, 0xFFFF, &CreateTempDC, &kClassCDC, nullptr};
inline constexpr CRuntimeClass kClassCTempGdiObject = /*00745F88*/ {"CTempGdiObject", 0x8, 0xFFFF, &CreateTempGdiObject, &kClassCGdiObject, nullptr};
inline constexpr CRuntimeClass kClassCTempMenu = /*007471B0*/ {"CTempMenu", 0x8, 0xFFFF, &CreateCTempMenu, &kClassCMenu, nullptr};
inline constexpr CRuntimeClass kClassCWnd = /*007469D8*/ {"CWnd", 0x3c, 0xFFFF, &CreateCWnd, &kClassCCmdTarget, nullptr};
inline constexpr CRuntimeClass kClassCTempWnd = /*007469B0*/ {"CTempWnd", 0x3c, 0xFFFF, &CreateCTempWnd, &kClassCWnd, nullptr};
inline constexpr CRuntimeClass kClassCUIntArray = /*00746778*/ {"CUIntArray", 0x14, 0xFFFF, nullptr, &kClassCObject, nullptr};
inline constexpr CRuntimeClass kClassCUserException = /*00746158*/ {"CUserException", 0x98, 0xFFFF, nullptr, &kClassCException, nullptr};
inline constexpr CRuntimeClass kClassCWinThread = /*007466B8*/ {"CWinThread", 0x68, 0xFFFF, nullptr, &kClassCCmdTarget, nullptr};
inline constexpr CRuntimeClass kClassCWinApp = /*00746FF8*/ {"CWinApp", 0xc0, 0xFFFF, nullptr, &kClassCWinThread, nullptr};
inline constexpr CRuntimeClass kClassCWindowDC = /*007460E8*/ {"CWindowDC", 0x14, 0xFFFF, nullptr, &kClassCDC, nullptr};

} // namespace mfc::impl
