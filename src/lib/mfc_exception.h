#pragma once

// MFC 4.2's exception classes as the game's copy throws them (except.cpp, filex.cpp, arcex.cpp),
// and AFX_EXCEPTION_LINK's chain (ticket S8, docs/notes/standalone.md section 2). The objects keep
// the exe's layout and vtables, and a throw is this compiler's `throw` of a pointer to mfc_try.h's
// class of the game's name, so a handler of ours catches it by that name. Every body is registered at its library address (mfc_exception.cpp) and checked in game by
// verify_mfc_exception.cpp until S21d.

#include "mfc_string.h"
#include "mfc_try.h"
#include "game/game_data.h"

namespace mfc::impl {

// The virtual slots every exception class has (CObject's three, then CException's two); declared only.
class __declspec(novtable) CExceptionVirtuals {
public:
    virtual void GetRuntimeClass();                                                 // 0
    virtual void* DeletingDestructor(unsigned flags);                               // 1
    virtual void Serialize();                                                       // 2
    virtual BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext);   // 3
    virtual int ReportError(unsigned type, unsigned error);                         // 4
};

// CException (8 bytes): vtable, m_bAutoDelete (a positive value: Delete frees the object).
struct CException {
    void* vtable;
    BOOL autoDelete;    // +0x04

    CException* Construct();                        // 00702053: auto-delete on, no vtable written
    CException* ConstructAutoDelete(BOOL autoDeleteFlag);   // 0070205D CException(BOOL): no vtable written
    // 0070209C (slot 4 of every exception class): the error message (slot 3) in a message box, or the
    // prompt `error` (0: AFX_IDP_NO_ERROR_AVAILABLE) when there is none; the box's answer. The help
    // context starts at 0 here (the original's is unset when slot 3 leaves it; the box ignores it).
    int ReportError(unsigned type, unsigned error);
};
static_assert(sizeof(CException) == 8);

// CSimpleException (0x98 bytes): the exceptions MFC keeps one static object of, their message loaded
// from the resource on first use (GetErrorMessage 006FE346).
struct CSimpleException {
    void* vtable;
    BOOL autoDelete;        // +0x04
    BOOL reserved;          // +0x08 (no code of the exe's touches it)
    BOOL initialized;       // +0x0C m_bInitialized (the message was looked up)
    BOOL loaded;            // +0x10 m_bLoaded (the message was found)
    char message[128];      // +0x14 m_szMessage
    unsigned resourceId;    // +0x94 m_nResourceID

    // The four classes' constructors (BOOL autoDelete, UINT resourceId): CException(BOOL), the flags
    // cleared, the resource id kept, the class's vtable (the one its destructor stores, below). Only the
    // static initialisers call them, on the static objects (resource ids AFX_IDS_RESOURCE_EXCEPTION ...).
    CSimpleException* ConstructResource(BOOL autoDeleteFlag, unsigned id);       // 006E68C9 (007C8C58, 0xF022)
    CSimpleException* ConstructUser(BOOL autoDeleteFlag, unsigned id);           // 006E6916 (007C8BC0, 0xF024)
    CSimpleException* ConstructMemory(BOOL autoDeleteFlag, unsigned id);         // 006E6AEA (007C8DE8, 0xF023)
    CSimpleException* ConstructNotSupported(BOOL autoDeleteFlag, unsigned id);   // 006E6B37 (007C8D50, 0xF021)

    // 006FE31D: the message loaded from string `resourceId` (initialized and loaded set).
    void InitString();
    // 006FE346 (slot 3 of the four classes): the message, loaded on first use, into `error` (empty when
    // it did not load); help context 0. Returns whether it loaded.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext);

private:
    CSimpleException* Construct(BOOL autoDeleteFlag, unsigned id, void* classVtable);
};
static_assert(sizeof(CSimpleException) == 0x98);

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

    static void* Vtable() { return ei::GamePointer<void>(0x00745E4C); }
    // An empty exception as CFile's constructors build it on the stack (cause none, no OS error),
    // on an object whose name is constructed.
    void InitEmpty();
    void Destruct();                                    // 006E6335
    CFileException* DeletingDestructor(unsigned flags); // 006E6319 (vtable slot 1)
    // 00703135 (slot 3): prompt AFX_IDP_FILE_NONE + cause with the file's name (AFX_IDS_UNNAMED_FILE's
    // text when it has none); the help context is that prompt's id. Always TRUE.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext);
};
static_assert(sizeof(CFileException) == 0x14);

// CArchiveException (0x10 bytes, vtable 00746808).
struct CArchiveException {
    enum Cause { none = 0, generic, readOnly, endOfFile, writeOnly, badIndex, badClass, badSchema };

    void* vtable;
    BOOL autoDelete;    // +0x04
    int cause;          // +0x08
    CString fileName;   // +0x0C

    static void* Vtable() { return ei::GamePointer<void>(0x00746808); }
    void Destruct();                                        // 006E6BA1
    CArchiveException* DeletingDestructor(unsigned flags);  // 006E6B85 (vtable slot 1)
    // 0070A795 (slot 3): as CFileException's, with AFX_IDP_ARCH_NONE + cause.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext);
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

// GetRuntimeClass of the two exception classes (mfc_runtime_class.h's records).
const void* __fastcall FileExceptionRuntimeClass(const void* self);     // 0070CF4C
const void* __fastcall ArchiveExceptionRuntimeClass(const void* self);  // 0070CF46

} // namespace mfc::impl

namespace mfc {

// AFX_EXCEPTION_LINK's two library functions (the link is mfc_try.h's ExceptionLink).
ExceptionLink* __fastcall ConstructExceptionLink(ExceptionLink* link);   // 007020F1
void __cdecl TryCleanup();                                               // 0070212F
// The calling thread's chain of links: the address of its top (AFX_EXCEPTION_CONTEXT::m_pLinkTop
// in MFC's thread state, 00702107). The hook build shares the original's; the image has its own.
ExceptionLink** __cdecl ExceptionLinkTop();

} // namespace mfc
