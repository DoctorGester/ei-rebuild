// MFC 4.2's exception classes and AFX_EXCEPTION_LINK, registered at their library addresses
// (mfc_exception.h).
#include "mfc_exception.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "mfc_resource.h"
#include "mfc_runtime_class.h"
#include "mfc_window.h"
#include "static_object.h"
#include "game/game_data.h"

#include <new>

namespace mfc::impl {

namespace {

// The static exceptions MFC throws by pointer, never deleted (_simpleResourceException, _simpleNotSupportedException,
// _simpleMemoryException; CUserException's is mfc_static_objects.h's): zero at the start, each built by its static
// initialiser in the list's order and destroyed by the exit function it registers (both ported on the name below and
// registered at their addresses; docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CResourceException> g_simpleResourceException = /*007C8C58*/ {};
constinit lib::StaticObject<CNotSupportedException> g_simpleNotSupportedException = /*007C8D50*/ {};
constinit lib::StaticObject<CMemoryException> g_simpleMemoryException = /*007C8DE8*/ {};

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

void* CException::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CException::operator delete(void* block) { gamecrt::operator_delete(block); }

int CException::ReportError(unsigned type, unsigned error) {
    char message[kReportLength];
    unsigned helpContext = 0;
    if (GetErrorMessage(message, kReportLength, &helpContext)) return MessageBoxText(message, type, helpContext);
    if (error == 0) error = kIdpNoErrorAvailable;
    return MessageBoxId(error, type, helpContext);
}

const CRuntimeClass* CResourceException::GetRuntimeClass() const { return &kClassCResourceException; }
const CRuntimeClass* CUserException::GetRuntimeClass() const { return &kClassCUserException; }
const CRuntimeClass* CMemoryException::GetRuntimeClass() const { return &kClassCMemoryException; }
const CRuntimeClass* CNotSupportedException::GetRuntimeClass() const { return &kClassCNotSupportedException; }
const CRuntimeClass* CFileException::GetRuntimeClass() const { return &kClassCFileException; }
const CRuntimeClass* CArchiveException::GetRuntimeClass() const { return &kClassCArchiveException; }

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
    if (exception->autoDelete > 0 && exception) delete exception;
}

CFileException::~CFileException() {}

BOOL CFileException::GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) {
    return CauseMessage(kIdpFileNone, cause, fileName, error, maxError, helpContext);
}

CArchiveException::~CArchiveException() {}

BOOL CArchiveException::GetErrorMessage(char* error, unsigned maxError, unsigned* helpContext) {
    return CauseMessage(kIdpArchiveNone, cause, fileName, error, maxError, helpContext);
}

void __fastcall DestroyUserException(CUserException* self) { self->~CUserException(); }

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
    auto* exception = new CFileException(cause, osError);
    if (exception) exception->fileName = fileName;
    throw reinterpret_cast<::CFileException*>(exception);
}

void __stdcall ThrowOsError(long osError, const char* fileName) {
    if (osError != 0) ThrowFileException(OsErrorToException(osError), osError, fileName);
}

void __stdcall ThrowArchiveException(int cause, const char* fileName) {
    auto* exception = new CArchiveException(cause);
    if (exception) exception->fileName = fileName;
    throw reinterpret_cast<::CArchiveException*>(exception);
}

void __stdcall ThrowNotSupported() {
    throw reinterpret_cast<::CNotSupportedException*>(&g_simpleNotSupportedException.Get());
}

void __stdcall ThrowMemoryException() {
    throw reinterpret_cast<::CMemoryException*>(&g_simpleMemoryException.Get());
}

void __stdcall ThrowResourceException() {
    throw reinterpret_cast<::CResourceException*>(&g_simpleResourceException.Get());
}

EI_REWRITTEN_SPECIAL(0x00702053, CException::CException);
EI_REWRITTEN_SPECIAL(0x0070205D, CException::CException);
EI_REWRITTEN_SPECIAL(0x006E68C9, CResourceException::CResourceException);
EI_REPLACE_METHOD(0x006E6916, CUserException::Construct);
EI_REWRITTEN_SPECIAL(0x006E6AEA, CMemoryException::CMemoryException);
EI_REWRITTEN_SPECIAL(0x006E6B37, CNotSupportedException::CNotSupportedException);
EI_REWRITTEN(0x00702069, DeleteException);
EI_REWRITTEN(0x0070209C, CException::ReportError);
EI_REWRITTEN(0x006FE31D, CSimpleException::InitString);
EI_REWRITTEN(0x006FE346, CSimpleException::GetErrorMessage);
EI_REWRITTEN(0x00703135, CFileException::GetErrorMessage);
EI_REWRITTEN(0x0070A795, CArchiveException::GetErrorMessage);
EI_REWRITTEN_SPECIAL(0x006E6335, CFileException::~CFileException);
EI_REWRITTEN_SPECIAL(0x006E6319, CFileException::~CFileException);   // deleting
EI_REWRITTEN_SPECIAL(0x006E6BA1, CArchiveException::~CArchiveException);
EI_REWRITTEN_SPECIAL(0x006E6B85, CArchiveException::~CArchiveException);   // deleting
EI_REWRITTEN_SPECIAL(0x006E690F, CResourceException::~CResourceException);
EI_REPLACE(0x006E695C, DestroyUserException);
EI_REWRITTEN_SPECIAL(0x006E6B30, CMemoryException::~CMemoryException);
EI_REWRITTEN_SPECIAL(0x006E6B7D, CNotSupportedException::~CNotSupportedException);
EI_REWRITTEN(0x00703296, OsErrorToException);
EI_REWRITTEN(0x007031D5, ThrowFileException);
EI_REWRITTEN(0x007030F0, ThrowOsError);
EI_REWRITTEN(0x0070A835, ThrowArchiveException);
EI_REWRITTEN(0x006FE3A2, ThrowNotSupported);
EI_REWRITTEN(0x006FE389, ThrowMemoryException);
EI_REWRITTEN(0x0070A29E, ThrowResourceException);
EI_REWRITTEN(0x0070CF4C, CFileException::GetRuntimeClass);
EI_REWRITTEN(0x0070CF46, CArchiveException::GetRuntimeClass);

namespace {
void __cdecl DestroySimpleResourceException() { g_simpleResourceException.Destroy(); }           // 0070BF22
void __cdecl DestroySimpleMemoryException() { g_simpleMemoryException.Destroy(); }               // 0070C1DD
void __cdecl DestroySimpleNotSupportedException() { g_simpleNotSupportedException.Destroy(); }   // 0070C215
void __cdecl ConstructSimpleResourceException() {   // 0070BEFA
    g_simpleResourceException.Construct(FALSE, kIdsResourceException);
    libcrt::AtExit(DestroySimpleResourceException);
}
void __cdecl ConstructSimpleMemoryException() {     // 0070C1B5
    g_simpleMemoryException.Construct(FALSE, kIdsMemoryException);
    libcrt::AtExit(DestroySimpleMemoryException);
}
void __cdecl ConstructSimpleNotSupportedException() {   // 0070C1ED
    g_simpleNotSupportedException.Construct(FALSE, kIdsNotSupportedException);
    libcrt::AtExit(DestroySimpleNotSupportedException);
}
} // namespace

EI_REPLACE(0x0070BEFA, ConstructSimpleResourceException);
EI_REPLACE(0x0070C1B5, ConstructSimpleMemoryException);
EI_REPLACE(0x0070C1ED, ConstructSimpleNotSupportedException);
EI_REPLACE(0x0070BF22, DestroySimpleResourceException);
EI_REPLACE(0x0070C1DD, DestroySimpleMemoryException);
EI_REPLACE(0x0070C215, DestroySimpleNotSupportedException);

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
