#pragma once

// MFC 4.2's exception classes as the game's copy throws them (except.cpp, filex.cpp, arcex.cpp), and
// AFX_EXCEPTION_LINK's chain (ticket S8, docs/notes/standalone.md section 2). The classes keep the exe's layout and
// slots (real C++ classes since C28n), and a throw is this compiler's `throw` of a pointer to mfc_try.h's class of
// the game's name, so a handler of ours catches it by that name.

#include "mfc_string.h"
#include "mfc_try.h"
#include "game/game_data.h"

#include <cstddef>
#include <new>

namespace mfc::impl {

struct CRuntimeClass;

// CException (8 bytes): CObject's three slots (0 GetRuntimeClass, 1 the deleting destructor, 2 Serialize), then
// CException's two (3 GetErrorMessage, 4 ReportError); m_bAutoDelete (a positive value: Delete frees the object).
// On the game's heap.
class CException {
public:
    // 00702053: auto-delete on.
    CException() : autoDelete(TRUE) {}
    // 0070205D: auto-delete as given.
    explicit CException(BOOL autoDeleteFlag) : autoDelete(autoDeleteFlag) {}

    virtual const CRuntimeClass* GetRuntimeClass() const = 0;                                // 0
    virtual ~CException() = default;                                                          // 1
    virtual void Serialize(void*) {}                                                          // 2: CObject's (00444B00)
    virtual BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) = 0;   // 3
    // 0070209C (4): the error message (slot 3) in a message box, or the prompt `error` (0: AFX_IDP_NO_ERROR_AVAILABLE)
    // when there is none; the box's answer. The help context starts at 0 here (the original's is unset when slot 3
    // leaves it; the box ignores it).
    virtual int ReportError(unsigned type, unsigned error);

    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    BOOL autoDelete;    // +0x04
};

// CSimpleException (0x98 bytes): the exceptions MFC keeps one static object of, their message loaded from the
// resource on first use (GetErrorMessage 006FE346).
class CSimpleException : public CException {
public:
    // The four classes' constructors (BOOL autoDelete, UINT resourceId): CException(BOOL), the flags cleared, the
    // resource id kept. Only the static initialisers build them, on the static objects (resource ids
    // AFX_IDS_RESOURCE_EXCEPTION ...).
    CSimpleException(BOOL autoDeleteFlag, unsigned id) : CException(autoDeleteFlag), resourceId(id) {}

    // 006FE346 (slot 3): the message, loaded on first use, into `error` (empty when it did not load); help context
    // 0. Returns whether it loaded.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) override;
    // 006FE31D: the message loaded from string `resourceId` (initialized and loaded set).
    void InitString();

    BOOL reserved;              // +0x08 (no code of the exe's touches it)
    BOOL initialized = FALSE;   // +0x0C m_bInitialized (the message was looked up)
    BOOL loaded = FALSE;        // +0x10 m_bLoaded (the message was found)
    char message[128];          // +0x14 m_szMessage
    unsigned resourceId;        // +0x94 m_nResourceID
};
static_assert(sizeof(CSimpleException) == 0x98);

// The four simple exceptions (game.exe's tables 0074646C, 00746484, 007465F8, 00746610; constructors 006E68C9,
// 006E6916, 006E6AEA, 006E6B37; destructors 006E690F, 006E695C, 006E6B30, 006E6B7D, nothing of their own).
class CResourceException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
};
class CUserException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
    // 006E6916 (thiscall, RET 8): the constructor on `this`, as the generated static initialiser builds MFC's
    // _simpleUserException there. Returns this.
    CUserException* Construct(BOOL autoDeleteFlag, unsigned id) { return new (this) CUserException(autoDeleteFlag, id); }
};
class CMemoryException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
};
class CNotSupportedException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
};

// CException::Delete (00702069; thiscall, called as fastcall): an auto-delete exception is deleted. The original
// reads the flag before testing the pointer.
void __fastcall DeleteException(CException* exception);

// CFileException (0x14 bytes, game.exe's table 00745E4C). The causes are MFC's CFileException::none ... endOfFile.
class CFileException final : public CException {
public:
    enum Cause {
        none = 0, generic, fileNotFound, badPath, tooManyOpenFiles, accessDenied, invalidFile, removeCurrentDir,
        directoryFull, badSeek, hardIO, sharingViolation, lockViolation, diskFull, endOfFile
    };

    // An empty exception (cause none, no OS error) as CFile's constructors build it on the stack, or the thrown one
    // (AfxThrowFileException).
    explicit CFileException(int causeValue = none, long osErrorValue = -1) : cause(causeValue), osError(osErrorValue) {}
    // 006E6335 (deleting 006E6319): the name released (a member).
    ~CFileException() override;
    // 0070CF4C (0).
    const CRuntimeClass* GetRuntimeClass() const override;
    // 00703135 (3): prompt AFX_IDP_FILE_NONE + cause with the file's name (AFX_IDS_UNNAMED_FILE's text when it has
    // none); the help context is that prompt's id. Always TRUE.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) override;

    int cause;          // +0x08 m_cause
    long osError;       // +0x0C m_lOsError (-1: none)
    CString fileName;   // +0x10 m_strFileName
};
static_assert(sizeof(CFileException) == 0x14);

// CArchiveException (0x10 bytes, game.exe's table 00746808).
class CArchiveException final : public CException {
public:
    enum Cause { none = 0, generic, readOnly, endOfFile, writeOnly, badIndex, badClass, badSchema };

    explicit CArchiveException(int causeValue) : cause(causeValue) {}
    // 006E6BA1 (deleting 006E6B85): the name released (a member).
    ~CArchiveException() override;
    // 0070CF46 (0).
    const CRuntimeClass* GetRuntimeClass() const override;
    // 0070A795 (3): as CFileException's, with AFX_IDP_ARCH_NONE + cause.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) override;

    int cause;          // +0x08
    CString fileName;   // +0x0C
};
static_assert(sizeof(CArchiveException) == 0x10);

// _simpleUserException's destructor (006E695C, reached by the generated exit function).
void __fastcall DestroyUserException(CUserException* self);

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

} // namespace mfc::impl

namespace mfc {

// AFX_EXCEPTION_LINK's two library functions (the link is mfc_try.h's ExceptionLink).
ExceptionLink* __fastcall ConstructExceptionLink(ExceptionLink* link);   // 007020F1
void __cdecl TryCleanup();                                               // 0070212F
// The calling thread's chain of links: the address of its top (AFX_EXCEPTION_CONTEXT::m_pLinkTop
// in MFC's thread state, 00702107). The hook build shares the original's; the image has its own.
ExceptionLink** __cdecl ExceptionLinkTop();

} // namespace mfc
