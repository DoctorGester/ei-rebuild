// Checks for MFC's worker threads (src/lib/mfc_thread.cpp, ticket S16):
// - CommonConstruct: the original on a copy, ours on the object, every byte but the cursor position
//   (the mouse may move between the two GetCursorPos calls) compared;
// - the CWinThread constructor: ours only (its SEH prologue calls within the first five bytes);
// - AfxBeginThread / AfxEndThread (image only): a start-up check starts a suspended thread through
//   ours, checks the object, keeps it (autoDelete cleared), resumes it and checks the procedure saw
//   its parameter and the thread's exit code is the procedure's result.
// The game's threads (sound stream, MIDI, cursor) start through the original AfxBeginThread in the
// hook build, so the two constructors are checked every start.
#include "verify.h"

#include "../../src/lib/crt.h"
#include "../../src/lib/mfc_thread.h"

#include "ei/hook.h"

#include <cstring>

using ei::verify::Check;
using mfc::impl::WinThread;

namespace {
void* s_unused;
} // namespace

// 0070C2F6: PUSH ESI; MOV ESI,ECX; PUSH EDI; XOR EDI,EDI (6).
static void(__fastcall* s_commonConstruct)(WinThread*);
static void __fastcall CommonConstructCheck(WinThread* self) {
    WinThread theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    s_commonConstruct(&theirs);
    self->CommonConstruct();
    theirs.cursorLast = self->cursorLast;
    Check("CWinThread::CommonConstruct", true, std::memcmp(&theirs, self, sizeof(theirs)) == 0, "");
}
EI_VERIFY(0x0070C2F6, CommonConstructCheck, 6, s_commonConstruct);

static WinThread* __fastcall ConstructCheck(WinThread* self, void*, mfc::impl::ThreadProc procedure, void* parameter) {
    WinThread* result = self->Construct(procedure, parameter);
    Check("CWinThread::CWinThread", true,
          result == self && self->base.vtable == WinThread::Vtable() && self->threadProc == procedure &&
              self->threadParams == parameter && self->autoDelete == TRUE && self->thread == nullptr &&
              self->base.dwRef == 1,
          "");
    return result;
}
EI_VERIFY(0x0070C259, ConstructCheck, 0, s_unused);

namespace {

void* s_seen = nullptr;

UINT __cdecl TestProcedure(void* parameter) {
    s_seen = parameter;
    return 7;
}

void ThreadStartupCheck() {
    int marker = 0;
    WinThread* thread = mfc::impl::AfxBeginThread(&TestProcedure, &marker, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED,
                                                  nullptr);
    const bool made = thread != nullptr && thread->base.vtable == WinThread::Vtable() && thread->thread != nullptr &&
                      thread->threadId != 0 && thread->threadProc == &TestProcedure && thread->threadParams == &marker;
    Check("AfxBeginThread (startup)", true, made, "");
    if (thread == nullptr) return;
    thread->autoDelete = FALSE;   // kept for the exit code
    ResumeThread(thread->thread);
    const DWORD waited = WaitForSingleObject(thread->thread, 5000);
    DWORD code = 0;
    GetExitCodeThread(thread->thread, &code);
    Check("AfxEndThread (startup)", true, waited == WAIT_OBJECT_0 && code == 7 && s_seen == &marker, "wait %lu, code %lu",
          waited, code);
    if (waited != WAIT_OBJECT_0) return;   // still running: leave it
    thread->autoDelete = TRUE;
    thread->Delete();
}

} // namespace

EI_STARTUP_CHECK(ThreadStartupCheck);
