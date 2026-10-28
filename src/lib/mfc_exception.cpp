// MFC 4.2's exception classes (mfc_exception.h).
#include "mfc_exception.h"

#include "gameapi.h"
#include "mfc_resource.h"
#include "mfc_runtime_class.h"
#include "static_object.h"
#include "game/game_data.h"

#include <cstdlib>
#include <new>

namespace mfc::impl {

namespace {

// The static exceptions MFC throws by pointer, never deleted (_simpleResourceException, _simpleMemoryException; CUserException's is mfc_static_objects.h's): zero at the start, each built by its static
// initialiser in the list's order and destroyed by the exit function it registers (
// docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CResourceException> g_simpleResourceException = /*007C8C58*/ {};
constinit lib::StaticObject<CMemoryException> g_simpleMemoryException = /*007C8DE8*/ {};

// ReportError's message buffer.
constexpr unsigned kReportLength = 512;

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. The game has no CWinApp, so the box has no
// owner and no help context and the module's file name as its title. MB_ICONEXCLAMATION is added to an OK, OK / Cancel,
// Yes / No / Cancel or Yes / No box that names no icon.
int MessageBoxText(const char* text, unsigned type, unsigned) {
    if (!(type & MB_ICONMASK)) {
        const unsigned buttons = type & MB_TYPEMASK;
        if (buttons == MB_OK || buttons == MB_OKCANCEL || buttons == MB_YESNOCANCEL || buttons == MB_YESNO) type |= MB_ICONEXCLAMATION;
    }
    char title[MAX_PATH];
    gameapi::GetModuleFileNameA(nullptr, title, MAX_PATH);
    return gameapi::MessageBoxA(nullptr, text, title, type);
}

// AfxMessageBox(UINT id, UINT type, UINT helpId): the box with string `id` of game.exe's table (empty when it is
// missing; MFC only traces that). A help id of -1 means `id`.
int MessageBoxId(unsigned promptId, unsigned type, unsigned helpId) {
    const std::string prompt = LoadResourceString(promptId);
    if (helpId == static_cast<unsigned>(-1)) helpId = promptId;
    return MessageBoxText(prompt.c_str(), type, helpId);
}

} // namespace

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

void DeleteException(CException* exception) {
    if (exception->autoDelete > 0 && exception) delete exception;
}


void DestroyUserException(CUserException* self) { self->~CUserException(); }


void ThrowMemoryException() {
    throw reinterpret_cast<::CMemoryException*>(&g_simpleMemoryException.Get());
}

void ThrowResourceException() {
    throw reinterpret_cast<::CResourceException*>(&g_simpleResourceException.Get());
}

namespace {
void DestroySimpleResourceException() { g_simpleResourceException.Destroy(); }
void DestroySimpleMemoryException() { g_simpleMemoryException.Destroy(); }
} // namespace

void ConstructSimpleResourceException() {
    g_simpleResourceException.Construct(FALSE, kIdsResourceException);
    std::atexit(DestroySimpleResourceException);
}
void ConstructSimpleMemoryException() {
    g_simpleMemoryException.Construct(FALSE, kIdsMemoryException);
    std::atexit(DestroySimpleMemoryException);
}

} // namespace mfc::impl
