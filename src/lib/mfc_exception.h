#pragma once

// MFC 4.2's exception classes as the game's copy throws them (except.cpp, filex.cpp, arcex.cpp),
// and AFX_EXCEPTION_LINK's chain (ticket S8, docs/notes/standalone.md section 2). The objects keep
// the exe's layout and vtables, and a throw goes through the game's _CxxThrowException with the
// exe's throw information, so a handler in original code and one in ours (mfc_try.h) catch it the
// same way. Every body is registered at its library address (mfc_exception.cpp) and checked by
// tests/verify/verify_mfc_exception.cpp.

#include "mfc_string.h"
#include "mfc_try.h"

namespace mfc::impl {

// CException (8 bytes): vtable, m_bAutoDelete (a positive value: Delete frees the object).
struct CException {
    void* vtable;
    BOOL autoDelete;    // +0x04

    CException* Construct();                        // 00702053: auto-delete on, no vtable written
};
static_assert(sizeof(CException) == 8);

// CSimpleException (0x98 bytes): MFC's four static exceptions (memory, not supported, resource,
// user), not auto-deleted; the message is loaded from the resource on first use. Built by the
// static initialisers through the classes' constructors (thiscall (bAutoDelete, nResourceID), RET 8,
// return this; ticket S16).
struct CSimpleException {
    void* vtable;
    BOOL autoDelete;            // +0x04
    std::uint32_t untouched08;  // not written by the constructors
    BOOL initialized;           // +0x0C m_bInitialized
    BOOL loaded;                // +0x10 m_bLoaded
    char message[128];          // +0x14 m_szMessage
    unsigned resourceId;        // +0x94 m_nResourceID

    CSimpleException* Construct(void* classVtable, BOOL autoDeleteFlag, unsigned id) {
        autoDelete = autoDeleteFlag;
        initialized = 0;
        loaded = 0;
        resourceId = id;
        vtable = classVtable;
        return this;
    }
};
static_assert(sizeof(CSimpleException) == 0x98 && offsetof(CSimpleException, resourceId) == 0x94);
// The four classes' vtables.
inline void* ResourceExceptionVtable() { return reinterpret_cast<void*>(0x0074646C); }
inline void* UserExceptionVtable() { return reinterpret_cast<void*>(0x00746484); }
inline void* MemoryExceptionVtable() { return reinterpret_cast<void*>(0x007465F8); }
inline void* NotSupportedExceptionVtable() { return reinterpret_cast<void*>(0x00746610); }
// 006E68C9 CResourceException, 006E6916 CUserException, 006E6AEA CMemoryException, 006E6B37
// CNotSupportedException (bAutoDelete, nResourceID).
CSimpleException* __fastcall ConstructResourceException(CSimpleException* self, void*, BOOL autoDelete, unsigned id);
CSimpleException* __fastcall ConstructUserException(CSimpleException* self, void*, BOOL autoDelete, unsigned id);
CSimpleException* __fastcall ConstructMemoryException(CSimpleException* self, void*, BOOL autoDelete, unsigned id);
CSimpleException* __fastcall ConstructNotSupportedException(CSimpleException* self, void*, BOOL autoDelete, unsigned id);

// CException::Delete (00702069; thiscall, called as fastcall): an auto-delete exception is
// deleted through vtable slot 1. The original reads the flag before testing the pointer.
void __fastcall DeleteException(CException* exception);

// CFileException (0x14 bytes, vtable 00745E4C). The causes are MFC's CFileException::none ...
// endOfFile.
struct CFileException {
    enum Cause {
        none = 0, generic, fileNotFound, badPath, tooManyOpenFiles, accessDenied, invalidFile, removeCurrentDir,
        directoryFull, badSeek, hardIO, sharingViolation, lockViolation, diskFull, endOfFile
    };

    void* vtable;
    BOOL autoDelete;    // +0x04
    int cause;          // +0x08 m_cause
    long osError;       // +0x0C m_lOsError (-1: none)
    CString fileName;   // +0x10 m_strFileName

    static void* Vtable() { return reinterpret_cast<void*>(0x00745E4C); }
    // An empty exception as CFile's constructors build it on the stack (cause none, no OS error),
    // on an object whose name is constructed.
    void InitEmpty();
    void Destruct();                                    // 006E6335
    CFileException* DeletingDestructor(unsigned flags); // 006E6319 (vtable slot 1)
};
static_assert(sizeof(CFileException) == 0x14);

// CArchiveException (0x10 bytes, vtable 00746808).
struct CArchiveException {
    enum Cause { none = 0, generic, readOnly, endOfFile, writeOnly, badIndex, badClass, badSchema };

    void* vtable;
    BOOL autoDelete;    // +0x04
    int cause;          // +0x08
    CString fileName;   // +0x0C

    static void* Vtable() { return reinterpret_cast<void*>(0x00746808); }
    void Destruct();                                        // 006E6BA1
    CArchiveException* DeletingDestructor(unsigned flags);  // 006E6B85 (vtable slot 1)
};
static_assert(sizeof(CArchiveException) == 0x10);

// The destructors of the simple exceptions (CSimpleException's and CException's are empty): each
// stores its class's vtable. The exit functions run them on MFC's static objects
// (_simpleResourceException 007C8C58, _simpleUserException 007C8BC0, _simpleMemoryException 007C8DE8,
// _simpleNotSupportedException 007C8D50); their deleting destructors call them too.
void __fastcall ResourceExceptionDestruct(CException* self);       // 006E690F (vtable 0074646C)
void __fastcall UserExceptionDestruct(CException* self);           // 006E695C (vtable 00746484)
void __fastcall MemoryExceptionDestruct(CException* self);         // 006E6B30 (vtable 007465F8)
void __fastcall NotSupportedExceptionDestruct(CException* self);   // 006E6B7D (vtable 00746610)

// CFileException::OsErrorToException: a Win32 error code as a cause (generic for the rest).
int __stdcall OsErrorToException(long osError);                                   // 00703296
// AfxThrowFileException: a new CFileException thrown as a pointer.
void __stdcall ThrowFileException(int cause, long osError, const char* fileName);  // 007031D5
// CFileException::ThrowOsError: nothing for 0, else the error's cause thrown.
void __stdcall ThrowOsError(long osError, const char* fileName);                   // 007030F0
// AfxThrowArchiveException.
void __stdcall ThrowArchiveException(int cause, const char* fileName);             // 0070A835
// AfxThrowNotSupportedException: MFC's one static CNotSupportedException (not auto-deleted).
void __stdcall ThrowNotSupported();                                                // 006FE3A2
// AfxThrowMemoryException: MFC's one static CMemoryException (not auto-deleted).
__declspec(noreturn) void __stdcall ThrowMemoryException();                         // 006FE389
// AfxThrowResourceException: MFC's one static CResourceException (not auto-deleted).
__declspec(noreturn) void __stdcall ThrowResourceException();                       // 0070A29E

// GetRuntimeClass of the two exception classes (their CRuntimeClass in the exe's data).
void* __fastcall FileExceptionRuntimeClass(const void* self);     // 0070CF4C
void* __fastcall ArchiveExceptionRuntimeClass(const void* self);  // 0070CF46

} // namespace mfc::impl

namespace mfc {

// AFX_EXCEPTION_LINK's two library functions (the link is mfc_try.h's ExceptionLink).
ExceptionLink* __fastcall ConstructExceptionLink(ExceptionLink* link);   // 007020F1
void __cdecl TryCleanup();                                               // 0070212F
// The calling thread's chain of links: the address of its top (AFX_EXCEPTION_CONTEXT::m_pLinkTop
// in MFC's thread state, 00702107). The hook build shares the original's; the image has its own.
ExceptionLink** __cdecl ExceptionLinkTop();

} // namespace mfc
