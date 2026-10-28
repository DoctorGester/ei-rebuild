#pragma once

// MFC 4.2's exception classes as the game's copy throws them (except.cpp, arcex.cpp; docs/notes/standalone.md
// section 2). The classes keep the exe's slots (real C++ classes; a file name is a std::string), and a throw is this
// compiler's `throw` of a pointer to mfc_try.h's class of the game's name.

#include "mfc.h"
#include "mfc_object.h"
#include "mfc_try.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstddef>
#include <new>
#include <string>

namespace mfc::impl {

// CException (8 bytes): CObject's three slots, then CException's two (3 GetErrorMessage, 4 ReportError);
// m_bAutoDelete (a positive value: Delete frees the object).
class CException : public CObject {
public:
    CException() : autoDelete(TRUE) {}
    explicit CException(BOOL autoDeleteFlag) : autoDelete(autoDeleteFlag) {}

    virtual BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) = 0;
    // The error message in a message box, or the prompt `error` (0: AFX_IDP_NO_ERROR_AVAILABLE)
    // when there is none; the box's answer. The help context starts at 0 here (unset in game.exe when slot 3
    // leaves it; the box ignores it).
    virtual int ReportError(unsigned type, unsigned error);

    BOOL autoDelete;
};

// CSimpleException (0x98 bytes): the exceptions MFC keeps one static object of, their message loaded from the
// resource on first use (GetErrorMessage).
class CSimpleException : public CException {
public:
    // The four classes' constructors (BOOL autoDelete, UINT resourceId): CException(BOOL), the flags cleared, the
    // resource id kept. Only the static initialisers build them, on the static objects (resource ids
    // AFX_IDS_RESOURCE_EXCEPTION ...).
    CSimpleException(BOOL autoDeleteFlag, unsigned id) : CException(autoDeleteFlag), resourceId(id) {}

    // The message, loaded on first use, into `error` (empty when it did not load); help context
    // 0. Returns whether it loaded.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) override;
    // The message loaded from string `resourceId` (initialized and loaded set).
    void InitString();

    BOOL reserved;              // (no code of the exe's touches it)
    BOOL initialized = FALSE;   // m_bInitialized (the message was looked up)
    BOOL loaded = FALSE;        // m_bLoaded (the message was found)
    char message[128];
    unsigned resourceId;
};
static_assert(sizeof(CSimpleException) == 0x98);

// The four simple exceptions (constructors and destructors only, nothing of their own).
class CResourceException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
};
class CUserException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
    // The constructor on `this`, as the generated static initialiser builds MFC's _simpleUserException there.
    CUserException* Construct(BOOL autoDeleteFlag, unsigned id) { return new (this) CUserException(autoDeleteFlag, id); }
};
class CMemoryException final : public CSimpleException {
public:
    using CSimpleException::CSimpleException;
    const CRuntimeClass* GetRuntimeClass() const override;
};

// CException::Delete (called as fastcall): an auto-delete exception is deleted. The flag
// is read before the pointer is tested.
void DeleteException(CException* exception);

// CArchiveException.
class CArchiveException final : public CException {
public:
    enum Cause { none = 0, generic, readOnly, endOfFile, writeOnly, badIndex, badClass, badSchema };

    explicit CArchiveException(int causeValue) : cause(causeValue) {}
    ~CArchiveException() override;
    const CRuntimeClass* GetRuntimeClass() const override;
    // Prompt AFX_IDP_ARCH_NONE + cause with the file's name (AFX_IDS_UNNAMED_FILE's text when it has none); the
    // help context is that prompt's id. Always TRUE.
    BOOL GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) override;

    int cause;
    std::string fileName;
};

// _simpleUserException's destructor (reached by the generated exit function).
void DestroyUserException(CUserException* self);

// AfxThrowArchiveException.
void ThrowArchiveException(int cause, const char* fileName);
// AfxThrowMemoryException: MFC's one static CMemoryException (not auto-deleted).
__declspec(noreturn) void ThrowMemoryException();
// AfxThrowResourceException: MFC's one static CResourceException (not auto-deleted).
__declspec(noreturn) void ThrowResourceException();

} // namespace mfc::impl
