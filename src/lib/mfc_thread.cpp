// MFC 4.2's worker threads (mfc_thread.h, ticket S16).
#include "mfc_thread.h"

#include "crt.h"
#include "gameapi.h"
#include "mfc_runtime_class.h"

#include "ei/hook.h"

#include <process.h>

namespace mfc::impl {

namespace {

// The thread object the current thread runs (MFC keeps it in the module thread state).
thread_local WinThread* t_current = nullptr;

// _AFX_THREAD_STARTUP: the creator waits on `started`, the new thread on `resume`.
struct Startup {
    WinThread* thread;
    HANDLE started;
    HANDLE resume;
};

// _AfxThreadEntry (0070215D): the thread object made current, the creator released, then (once the
// creator has set the priority) the procedure, and AfxEndThread with its result. The original runs
// InitInstance / Run for a thread without a procedure; the game has none.
unsigned __stdcall ThreadEntry(void* argument) {
    Startup* startup = static_cast<Startup*>(argument);
    WinThread* thread = startup->thread;
    HANDLE resume = startup->resume;
    t_current = thread;
    gameapi::SetEvent(startup->started);   // `startup` is the creator's; gone after this
    gameapi::WaitForSingleObject(resume, INFINITE);
    gameapi::CloseHandle(resume);
    const UINT result = thread->threadProc ? thread->threadProc(thread->threadParams) : 0;
    AfxEndThread(result, TRUE);
    return 0;
}

// CWinThread::CreateThread (007024B4): FALSE when an event or the thread cannot be made.
BOOL CreateThread(WinThread* thread, DWORD flags, UINT stackSize, LPSECURITY_ATTRIBUTES security) {
    Startup startup{thread, gameapi::CreateEventA(nullptr, TRUE, FALSE, nullptr),
                    gameapi::CreateEventA(nullptr, TRUE, FALSE, nullptr)};
    if (startup.started == nullptr || startup.resume == nullptr) {
        if (startup.started) gameapi::CloseHandle(startup.started);
        if (startup.resume) gameapi::CloseHandle(startup.resume);
        return FALSE;
    }
    unsigned id = 0;
    thread->thread = reinterpret_cast<HANDLE>(
        _beginthreadex(security, stackSize, &ThreadEntry, &startup, flags | CREATE_SUSPENDED, &id));
    thread->threadId = id;
    if (thread->thread == nullptr) {
        gameapi::CloseHandle(startup.started);
        gameapi::CloseHandle(startup.resume);
        return FALSE;
    }
    gameapi::ResumeThread(thread->thread);
    gameapi::WaitForSingleObject(startup.started, INFINITE);
    gameapi::CloseHandle(startup.started);
    if (flags & CREATE_SUSPENDED) gameapi::SuspendThread(thread->thread);
    gameapi::SetEvent(startup.resume);
    return TRUE;
}

// CWinThread's 31 slots (C19; MFC 4.2's CWinThread virtuals): CCmdTarget's 20, then the thread's, unported but
// Delete: the image's threads run their procedure only (ThreadEntry), never InitInstance / Run or a message loop.
struct WinThreadTable final : CmdTargetTable<WinThreadTable, &kClassCWinThread> {
    void* DeleteScalar(unsigned flags) override { return This()->DeletingDestructor(flags); }    // 1: 0070C29D
    void OnFinalRelease() override { ei::UnportedSlot(0x00704179); }                             // 4
    const void* GetMessageMap() const override { ei::UnportedSlot(0x00704161); }                 // 10
    virtual BOOL InitInstance() { ei::UnportedSlot(0x007025A8); }                                // 20
    virtual int Run() { ei::UnportedSlot(0x007025AB); }                                          // 21
    virtual BOOL PreTranslateMessage(MSG*) { ei::UnportedSlot(0x00702819); }                     // 22
    virtual BOOL PumpMessage() { ei::UnportedSlot(0x00702ACE); }                                 // 23
    virtual BOOL OnIdle(LONG) { ei::UnportedSlot(0x00702687); }                                  // 24
    virtual BOOL IsIdleMessage(MSG*) { ei::UnportedSlot(0x0070262A); }                           // 25
    virtual int ExitInstance() { ei::UnportedSlot(0x00702683); }                                 // 26
    virtual LRESULT ProcessWndProcException(void*, const MSG*) { ei::UnportedSlot(0x00702876); } // 27
    virtual BOOL ProcessMessageFilter(int, MSG*) { ei::UnportedSlot(0x00702969); }               // 28
    virtual CWnd* GetMainWnd() { ei::UnportedSlot(0x00702AB3); }                                 // 29
    virtual void Delete() { This()->Delete(); }                                                  // 30: 00702596

private:
    WinThread* This() { return ObjectAs<WinThread>(); }
};

} // namespace

void* WinThread::Vtable() { return WinThreadTable::Vtable(); }

WinThread* WinThread::Construct(ThreadProc procedure, void* parameter) {
    base.Construct();
    base.vtable = Vtable();
    threadProc = procedure;
    threadParams = parameter;
    CommonConstruct();
    return this;
}
EI_REWRITTEN(0x0070C259, WinThread::Construct);

void WinThread::CommonConstruct() {
    mainWnd = nullptr;
    activeWnd = nullptr;
    thread = nullptr;
    threadId = 0;
    msgCur.message = WM_NULL;
    msgLast = WM_NULL;
    gameapi::GetCursorPos(&cursorLast);
    messageFilter = nullptr;
    oleTermOrFreeLib = nullptr;
    autoDelete = TRUE;
}
EI_REWRITTEN(0x0070C2F6, WinThread::CommonConstruct);

// The original deletes through slot 1, which no class of the game overrides.
void WinThread::Delete() {
    if (autoDelete) DeletingDestructor(1);
}

void WinThread::Destruct() {
    base.vtable = Vtable();
    if (thread != nullptr) gameapi::CloseHandle(thread);
    if (t_current == this) t_current = nullptr;
    base.Destruct();
}

WinThread* WinThread::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00702596, WinThread::Delete);
EI_REWRITTEN(0x0070E132, WinThread::Destruct);
EI_REWRITTEN(0x0070C29D, WinThread::DeletingDestructor);

WinThread* __stdcall AfxBeginThread(ThreadProc procedure, void* parameter, int priority, UINT stackSize,
                                    DWORD flags, LPSECURITY_ATTRIBUTES security) {
    WinThread* thread = static_cast<WinThread*>(gamecrt::operator_new(sizeof(WinThread)));
    thread->Construct(procedure, parameter);
    if (!CreateThread(thread, flags | CREATE_SUSPENDED, stackSize, security)) {
        thread->Delete();
        return nullptr;
    }
    gameapi::SetThreadPriority(thread->thread, priority);
    if (!(flags & CREATE_SUSPENDED)) gameapi::ResumeThread(thread->thread);
    return thread;
}
EI_REWRITTEN(0x007022A0, AfxBeginThread);

void __stdcall AfxEndThread(UINT exitCode, BOOL deleteThread) {
    if (WinThread* thread = t_current) {
        if (thread->oleTermOrFreeLib) thread->oleTermOrFreeLib(TRUE, FALSE);
        if (deleteThread) thread->Delete();
        t_current = nullptr;
    }
    _endthreadex(exitCode);
}
EI_REWRITTEN(0x00702380, AfxEndThread);

} // namespace mfc::impl
