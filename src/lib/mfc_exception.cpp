// MFC 4.2's exception classes and AFX_EXCEPTION_LINK, registered at their library addresses
// (mfc_exception.h).
#include "mfc_exception.h"

#include "ei/hook.h"
#include "game/originals_library.h"   // CxxThrowException
#include "gameapi.h"
#include "mfc_resource.h"
#include "mfc_window.h"
#include "game/game_data.h"

#include <new>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

// The exe's throw information for a CFileException* and a CArchiveException* (the pointer to the
// class and to each base, ".PAVCFileException@@" ... ".PAX").
const void* FileExceptionThrowInfo() { return ei::GamePointer<const void>(0x0077F440); }
const void* ArchiveExceptionThrowInfo() { return ei::GamePointer<const void>(0x0077F2B0); }
// _simpleNotSupportedException (a static CNotSupportedException) and the throw information of a
// CNotSupportedException*.
void* SimpleNotSupportedException() { return ei::GamePointer<void>(0x007C8D50); }
const void* NotSupportedThrowInfo() { return ei::GamePointer<const void>(0x0077EE80); }
// _simpleMemoryException and _simpleResourceException (static, not auto-deleted) and the throw
// information of a CMemoryException* and a CResourceException*.
void* SimpleMemoryException() { return ei::GamePointer<void>(0x007C8DE8); }
const void* MemoryThrowInfo() { return ei::GamePointer<const void>(0x0077EE38); }
void* SimpleResourceException() { return ei::GamePointer<void>(0x007C8C58); }
const void* ResourceThrowInfo() { return ei::GamePointer<const void>(0x0077EC28); }

// The two classes' CRuntimeClass records.
void* FileExceptionClass() { return ei::GamePointer<void>(0x00746820); }
void* ArchiveExceptionClass() { return ei::GamePointer<void>(0x007467D8); }

mfc::CObjectVirtuals* Virtuals(CException* exception) { return reinterpret_cast<mfc::CObjectVirtuals*>(exception); }
CExceptionVirtuals* Slots(CException* exception) { return reinterpret_cast<CExceptionVirtuals*>(exception); }

// ReportError's message buffer.
constexpr unsigned kReportLength = 512;

// GetErrorMessage of the file and archive exceptions: prompt `base` + cause with the file's name.
BOOL CauseMessage(unsigned base, int cause, const CString& fileName, char* error, unsigned maxError, unsigned* helpContext) {
    const unsigned prompt = base + static_cast<unsigned>(cause);
    if (helpContext) *helpContext = prompt;
    CString message;
    CString name(fileName);
    if (name.IsEmpty()) name.LoadResourceString(kIdsUnnamedFile);
    AfxFormatString1(message, prompt, name);
    gameapi::lstrcpynA(error, message, static_cast<int>(maxError));
    return TRUE;
}

} // namespace

CException* CException::Construct() {
    autoDelete = TRUE;
    return this;
}

CException* CException::ConstructAutoDelete(BOOL autoDeleteFlag) {
    autoDelete = autoDeleteFlag;
    return this;
}

int CException::ReportError(unsigned type, unsigned error) {
    char message[kReportLength];
    unsigned helpContext = 0;
    if (Slots(this)->GetErrorMessage(message, kReportLength, &helpContext)) return MessageBoxText(message, type, helpContext);
    if (error == 0) error = kIdpNoErrorAvailable;
    return MessageBoxId(error, type, helpContext);
}

CSimpleException* CSimpleException::Construct(BOOL autoDeleteFlag, unsigned id, void* classVtable) {
    reinterpret_cast<CException*>(this)->ConstructAutoDelete(autoDeleteFlag);
    initialized = FALSE;
    loaded = FALSE;
    resourceId = id;
    vtable = classVtable;
    return this;
}

CSimpleException* CSimpleException::ConstructResource(BOOL autoDeleteFlag, unsigned id) {
    return Construct(autoDeleteFlag, id, ei::GamePointer<void>(0x0074646C));
}
CSimpleException* CSimpleException::ConstructUser(BOOL autoDeleteFlag, unsigned id) {
    return Construct(autoDeleteFlag, id, ei::GamePointer<void>(0x00746484));
}
CSimpleException* CSimpleException::ConstructMemory(BOOL autoDeleteFlag, unsigned id) {
    return Construct(autoDeleteFlag, id, ei::GamePointer<void>(0x007465F8));
}
CSimpleException* CSimpleException::ConstructNotSupported(BOOL autoDeleteFlag, unsigned id) {
    return Construct(autoDeleteFlag, id, ei::GamePointer<void>(0x00746610));
}

void CSimpleException::InitString() {
    initialized = TRUE;
    loaded = AfxLoadString(resourceId, message, sizeof message) != 0 ? TRUE : FALSE;
}

BOOL CSimpleException::GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) {
    if (helpContext) *helpContext = 0;
    if (!initialized) InitString();
    if (loaded)
        gameapi::lstrcpynA(error, message, static_cast<int>(maxError));
    else
        error[0] = '\0';
    return loaded;
}

void __fastcall DeleteException(CException* exception) {
    if (exception->autoDelete > 0 && exception) Virtuals(exception)->DeletingDestructor(1);
}

void CFileException::InitEmpty() {
    reinterpret_cast<CException*>(this)->Construct();
    cause = none;
    osError = -1;
    vtable = Vtable();
    fileName = static_cast<const char*>(nullptr);
}

void CFileException::Destruct() {
    vtable = Vtable();
    fileName.~CString();
}

CFileException* CFileException::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CFileException::GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) {
    return CauseMessage(kIdpFileNone, cause, fileName, error, maxError, helpContext);
}

void CArchiveException::Destruct() {
    vtable = Vtable();
    fileName.~CString();
}

CArchiveException* CArchiveException::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CArchiveException::GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) {
    return CauseMessage(kIdpArchiveNone, cause, fileName, error, maxError, helpContext);
}

void __fastcall ResourceExceptionDestruct(CException* self) { self->vtable = ei::GamePointer<void>(0x0074646C); }
void __fastcall UserExceptionDestruct(CException* self) { self->vtable = ei::GamePointer<void>(0x00746484); }
void __fastcall MemoryExceptionDestruct(CException* self) { self->vtable = ei::GamePointer<void>(0x007465F8); }
void __fastcall NotSupportedExceptionDestruct(CException* self) { self->vtable = ei::GamePointer<void>(0x00746610); }

int __stdcall OsErrorToException(long osError) {
    switch (static_cast<unsigned long>(osError)) {
    case NO_ERROR: return CFileException::none;
    case ERROR_FILE_NOT_FOUND: return CFileException::fileNotFound;
    case ERROR_PATH_NOT_FOUND: return CFileException::badPath;
    case ERROR_TOO_MANY_OPEN_FILES: return CFileException::tooManyOpenFiles;
    case ERROR_ACCESS_DENIED: return CFileException::accessDenied;
    case ERROR_INVALID_HANDLE: return CFileException::fileNotFound;
    case ERROR_BAD_FORMAT: return CFileException::invalidFile;
    case ERROR_INVALID_ACCESS: return CFileException::accessDenied;
    case ERROR_INVALID_DRIVE: return CFileException::badPath;
    case ERROR_CURRENT_DIRECTORY: return CFileException::removeCurrentDir;
    case ERROR_NOT_SAME_DEVICE: return CFileException::badPath;
    case ERROR_NO_MORE_FILES: return CFileException::fileNotFound;
    case ERROR_WRITE_PROTECT: return CFileException::accessDenied;
    case ERROR_BAD_UNIT: return CFileException::hardIO;
    case ERROR_NOT_READY: return CFileException::hardIO;
    case ERROR_BAD_COMMAND: return CFileException::hardIO;
    case ERROR_CRC: return CFileException::hardIO;
    case ERROR_BAD_LENGTH: return CFileException::badSeek;
    case ERROR_SEEK: return CFileException::badSeek;
    case ERROR_NOT_DOS_DISK: return CFileException::invalidFile;
    case ERROR_SECTOR_NOT_FOUND: return CFileException::badSeek;
    case ERROR_WRITE_FAULT: return CFileException::accessDenied;
    case ERROR_READ_FAULT: return CFileException::badSeek;
    case ERROR_SHARING_VIOLATION: return CFileException::sharingViolation;
    case ERROR_LOCK_VIOLATION: return CFileException::lockViolation;
    case ERROR_WRONG_DISK: return CFileException::badPath;
    case ERROR_SHARING_BUFFER_EXCEEDED: return CFileException::tooManyOpenFiles;
    case ERROR_HANDLE_EOF: return CFileException::endOfFile;
    case ERROR_HANDLE_DISK_FULL: return CFileException::diskFull;
    case ERROR_DUP_NAME: return CFileException::badPath;
    case ERROR_BAD_NETPATH: return CFileException::badPath;
    case ERROR_NETWORK_BUSY: return CFileException::accessDenied;
    case ERROR_DEV_NOT_EXIST: return CFileException::badPath;
    case ERROR_ADAP_HDW_ERR: return CFileException::hardIO;
    case ERROR_BAD_NET_RESP: return CFileException::accessDenied;
    case ERROR_UNEXP_NET_ERR: return CFileException::hardIO;
    case ERROR_BAD_REM_ADAP: return CFileException::invalidFile;
    case ERROR_NO_SPOOL_SPACE: return CFileException::directoryFull;
    case ERROR_NETNAME_DELETED: return CFileException::accessDenied;
    case ERROR_NETWORK_ACCESS_DENIED: return CFileException::accessDenied;
    case ERROR_BAD_DEV_TYPE: return CFileException::invalidFile;
    case ERROR_BAD_NET_NAME: return CFileException::badPath;
    case ERROR_TOO_MANY_NAMES: return CFileException::tooManyOpenFiles;
    case ERROR_SHARING_PAUSED: return CFileException::badPath;
    case ERROR_REQ_NOT_ACCEP: return CFileException::accessDenied;
    case ERROR_FILE_EXISTS: return CFileException::accessDenied;
    case ERROR_CANNOT_MAKE: return CFileException::accessDenied;
    case ERROR_ALREADY_ASSIGNED: return CFileException::badPath;
    case ERROR_INVALID_PASSWORD: return CFileException::accessDenied;
    case ERROR_NET_WRITE_FAULT: return CFileException::hardIO;
    case ERROR_DISK_CHANGE: return CFileException::fileNotFound;
    case ERROR_DRIVE_LOCKED: return CFileException::lockViolation;
    case ERROR_BUFFER_OVERFLOW: return CFileException::badPath;
    case ERROR_DISK_FULL: return CFileException::diskFull;
    case ERROR_NO_MORE_SEARCH_HANDLES: return CFileException::tooManyOpenFiles;
    case ERROR_INVALID_TARGET_HANDLE: return CFileException::invalidFile;
    case ERROR_INVALID_CATEGORY: return CFileException::hardIO;
    case ERROR_INVALID_NAME: return CFileException::badPath;
    case ERROR_INVALID_LEVEL: return CFileException::badPath;
    case ERROR_NO_VOLUME_LABEL: return CFileException::badPath;
    case ERROR_NEGATIVE_SEEK: return CFileException::badSeek;
    case ERROR_SEEK_ON_DEVICE: return CFileException::badSeek;
    case ERROR_DIR_NOT_ROOT: return CFileException::badPath;
    case ERROR_DIR_NOT_EMPTY: return CFileException::removeCurrentDir;
    case ERROR_LABEL_TOO_LONG: return CFileException::badPath;
    case ERROR_BAD_PATHNAME: return CFileException::badPath;
    case ERROR_LOCK_FAILED: return CFileException::lockViolation;
    case ERROR_BUSY: return CFileException::accessDenied;
    case ERROR_INVALID_ORDINAL: return CFileException::invalidFile;
    case ERROR_ALREADY_EXISTS: return CFileException::accessDenied;
    case ERROR_INVALID_EXE_SIGNATURE: return CFileException::invalidFile;
    case ERROR_BAD_EXE_FORMAT: return CFileException::invalidFile;
    case ERROR_FILENAME_EXCED_RANGE: return CFileException::badPath;
    case ERROR_META_EXPANSION_TOO_LONG: return CFileException::badPath;
    case ERROR_DIRECTORY: return CFileException::badPath;
    case ERROR_OPERATION_ABORTED: return CFileException::hardIO;
    case ERROR_IO_INCOMPLETE: return CFileException::hardIO;
    case ERROR_IO_PENDING: return CFileException::hardIO;
    case ERROR_SWAPERROR: return CFileException::accessDenied;
    default: return CFileException::generic;
    }
}

void __stdcall ThrowFileException(int cause, long osError, const char* fileName) {
    auto* exception = static_cast<CFileException*>(gamecrt::operator_new(sizeof(CFileException)));
    if (exception) {
        reinterpret_cast<CException*>(exception)->Construct();
        new (&exception->fileName) CString();
        exception->cause = cause;
        exception->vtable = CFileException::Vtable();
        exception->osError = osError;
        exception->fileName = fileName;
    }
    CFileException* thrown = exception;
    libcrt::CxxThrowException(&thrown, FileExceptionThrowInfo());
}

void __stdcall ThrowOsError(long osError, const char* fileName) {
    if (osError != 0) ThrowFileException(OsErrorToException(osError), osError, fileName);
}

void __stdcall ThrowArchiveException(int cause, const char* fileName) {
    auto* exception = static_cast<CArchiveException*>(gamecrt::operator_new(sizeof(CArchiveException)));
    if (exception) {
        reinterpret_cast<CException*>(exception)->Construct();
        new (&exception->fileName) CString();
        exception->vtable = CArchiveException::Vtable();
        exception->cause = cause;
        exception->fileName = fileName;
    }
    CArchiveException* thrown = exception;
    libcrt::CxxThrowException(&thrown, ArchiveExceptionThrowInfo());
}

void __stdcall ThrowNotSupported() {
    void* thrown = SimpleNotSupportedException();
    libcrt::CxxThrowException(&thrown, NotSupportedThrowInfo());
}

void __stdcall ThrowMemoryException() {
    void* thrown = SimpleMemoryException();
    libcrt::CxxThrowException(&thrown, MemoryThrowInfo());
    __assume(false);
}

void __stdcall ThrowResourceException() {
    void* thrown = SimpleResourceException();
    libcrt::CxxThrowException(&thrown, ResourceThrowInfo());
    __assume(false);
}

void* __fastcall FileExceptionRuntimeClass(const void*) { return FileExceptionClass(); }
void* __fastcall ArchiveExceptionRuntimeClass(const void*) { return ArchiveExceptionClass(); }

EI_REWRITTEN(0x00702053, CException::Construct);
EI_REWRITTEN(0x0070205D, CException::ConstructAutoDelete);
EI_REPLACE_METHOD(0x006E68C9, CSimpleException::ConstructResource);
EI_REPLACE_METHOD(0x006E6916, CSimpleException::ConstructUser);
EI_REPLACE_METHOD(0x006E6AEA, CSimpleException::ConstructMemory);
EI_REPLACE_METHOD(0x006E6B37, CSimpleException::ConstructNotSupported);
EI_REWRITTEN(0x00702069, DeleteException);
EI_REPLACE_METHOD(0x0070209C, CException::ReportError);
EI_REWRITTEN(0x006FE31D, CSimpleException::InitString);
EI_REPLACE_METHOD(0x006FE346, CSimpleException::GetErrorMessage);
EI_REPLACE_METHOD(0x00703135, CFileException::GetErrorMessage);
EI_REPLACE_METHOD(0x0070A795, CArchiveException::GetErrorMessage);
EI_REWRITTEN(0x006E6335, CFileException::Destruct);
EI_REPLACE_METHOD(0x006E6319, CFileException::DeletingDestructor);
EI_REWRITTEN(0x006E6BA1, CArchiveException::Destruct);
EI_REPLACE_METHOD(0x006E6B85, CArchiveException::DeletingDestructor);
EI_REPLACE(0x006E690F, ResourceExceptionDestruct);
EI_REPLACE(0x006E695C, UserExceptionDestruct);
EI_REPLACE(0x006E6B30, MemoryExceptionDestruct);
EI_REPLACE(0x006E6B7D, NotSupportedExceptionDestruct);
EI_REWRITTEN(0x00703296, OsErrorToException);
EI_REWRITTEN(0x007031D5, ThrowFileException);
EI_REWRITTEN(0x007030F0, ThrowOsError);
EI_REWRITTEN(0x0070A835, ThrowArchiveException);
EI_REWRITTEN(0x006FE3A2, ThrowNotSupported);
EI_REWRITTEN(0x006FE389, ThrowMemoryException);
EI_REWRITTEN(0x0070A29E, ThrowResourceException);
EI_REPLACE(0x0070CF4C, FileExceptionRuntimeClass);
EI_REPLACE(0x0070CF46, ArchiveExceptionRuntimeClass);

} // namespace mfc::impl

namespace mfc {

namespace {
// MFC's thread state lookup: the address of the thread's top link (the original's in the hook
// build, ExceptionLinkTop below in the image).
} // namespace

ExceptionLink** __cdecl ExceptionLinkTop() {
    static thread_local ExceptionLink* top = nullptr;
    return &top;
}

ExceptionLink* __fastcall ConstructExceptionLink(ExceptionLink* link) {
    link->exception = nullptr;
    ExceptionLink** top = ExceptionLinkTop();
    link->previous = *top;
    *top = link;
    return link;
}

void __cdecl TryCleanup() {
    ExceptionLink** top = ExceptionLinkTop();
    ExceptionLink* link = *top;
    if (link->exception) impl::DeleteException(static_cast<impl::CException*>(link->exception));
    *top = link->previous;
}

EI_REWRITTEN(0x007020F1, ConstructExceptionLink);
EI_REWRITTEN(0x0070212F, TryCleanup);
EI_REWRITTEN(0x00702107, ExceptionLinkTop);
} // namespace mfc
