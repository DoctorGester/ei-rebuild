#pragma once

// MFC 4.2's static data that the static initialisers build (mfc_module.cpp, ticket S16): the
// runtime-class registration (AFX_CLASSINIT), the constructors of the four static CWnds (wndTop,
// wndBottom, wndTopMost, wndNoTopMost) and of CCmdTarget, the DBCS flag and the GDI terminator's
// registration. afxData (AUX_DATA) and the destructors are S3d's (mfc_state.h, mfc_window.h). The
// hook build keeps the original's module state; the image keeps a class list of its own.

#include "mfc_state.h"
#include "mfc_window.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace mfc::impl {

// CRuntimeClass (statically linked MFC: the base class by pointer, 0x18 bytes).
struct CRuntimeClass {
    const char* className;        // +0x00
    int objectSize;               // +0x04
    unsigned schema;              // +0x08
    void* createObject;           // +0x0C
    CRuntimeClass* baseClass;     // +0x10
    CRuntimeClass* nextClass;     // +0x14 the module's class list
};
static_assert(sizeof(CRuntimeClass) == 0x18 && offsetof(CRuntimeClass, nextClass) == 0x14);

// CSimpleList: the head and the offset of each element's next link.
struct SimpleList {
    void* head;
    std::size_t nextOffset;

    // 0070C4E9 (thiscall, RET 4): `p`'s link to the old head, `p` the new head.
    void AddHead(void* p) {
        *reinterpret_cast<void**>(static_cast<unsigned char*>(p) + nextOffset) = head;
        head = p;
    }
};

// The image's module class list (AFX_MODULE_STATE::m_classList; empty until the initialisers run).
SimpleList& ImageClassList();

// 00700674 AfxClassInit (stdcall, RET 4): the class at the head of the module's class list (under
// the runtime-class lock, which the image's single-threaded start does not need). Image only.
void __stdcall AfxClassInit(CRuntimeClass* newClass);

// 00703EAE CCmdTarget's constructor (thiscall, returns this; no vtable written): one reference, no
// OLE interfaces, a result expected.
CCmdTarget* ConstructCmdTarget(CCmdTarget* target);
// 00704546 CWnd(HWND) (thiscall, RET 4, returns this): CCmdTarget's constructor, the vtable, the
// members after CCmdTarget zeroed, then the handle.
CWnd* ConstructWnd(CWnd* wnd, HWND window);

// 0070DC4D (cdecl): whether the OEM code page has multibyte characters (MaxCharSize above 1);
// stored by its initialiser in 007CAA98.
BOOL __cdecl OemCodePageIsDbcs();

// 0070B5F0 (cdecl, the exit function): AfxDeleteObject on the GDI handle at 007C8D40.
void __cdecl AfxGdiTerm();
// 0070B600 (cdecl, a static initialiser): AfxGdiTerm registered with atexit, its result kept in
// the byte at 007C8D44.
void __cdecl AfxGdiTermInit();

} // namespace mfc::impl
