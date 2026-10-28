#pragma once

// MFC 4.2's worker threads as the game starts them: AfxBeginThread with a thread procedure (the sound stream,
// the MIDI player, the cursor) and AfxEndThread. The threads come from the host CRT's _beginthreadex and need
// no MFC thread state: the procedures never pump messages or open dialogs, so AfxInitThread's message filter
// hook and AfxTermThread's temporary maps are left out.

#include "mfc_window.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstddef>

namespace mfc::impl {

using ThreadProc = UINT(*)(void* parameter);

// CWinThread (0x68 bytes; CCmdTarget's 20 slots, then the thread's; the destructor and
// Delete are here, the message loop is not: the game's threads only run their procedure on a host thread
// (AfxBeginThread), never InitInstance / Run or a message loop; the game reads only m_hThread).
struct WinThread : CCmdTarget {
    // CCmdTarget's constructor, the procedure and its parameter, then CommonConstruct.
    WinThread(ThreadProc procedure, void* parameter);
    // ~CWinThread (deleting): the thread handle closed, no longer the current thread, then
    // CCmdTarget's destructor.
    ~WinThread() override;

    const CRuntimeClass* GetRuntimeClass() const override;
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
    // Deleted when autoDelete is set.
    virtual void Delete();

    // No windows, no thread yet, the message state cleared, the cursor position
    // (GetCursorPos), no filter or OLE hook, deleted when its thread ends.
    void CommonConstruct();

    void* mainWnd;
    void* activeWnd;
    BOOL autoDelete;
    HANDLE thread;
    DWORD threadId;
    MSG msgCur;
    void* threadParams;
    ThreadProc threadProc;
    void(*oleTermOrFreeLib)(BOOL, BOOL);
    void* messageFilter;
    POINT cursorLast;
    UINT msgLast;
};
static_assert(sizeof(WinThread) == 0x68);
static_assert(offsetof(WinThread, thread) == 0x28 && offsetof(WinThread, threadParams) == 0x4C);
static_assert(offsetof(WinThread, cursorLast) == 0x5C && offsetof(WinThread, msgLast) == 0x64);

// AfxBeginThread: a new CWinThread for `procedure`, its thread created suspended
// (CreateThread: started, then held until the creator has its handle), the priority set,
// then resumed unless `flags` has CREATE_SUSPENDED. Null when the thread cannot be created. Image only.
WinThread* AfxBeginThread(ThreadProc procedure, void* parameter, int priority, UINT stackSize,
                                    DWORD flags, LPSECURITY_ATTRIBUTES security);
// AfxEndThread (does not return): the current thread's OLE hook, then its object
// deleted when `deleteThread` (and autoDelete) is set, then _endthreadex(exitCode). Image only.
void AfxEndThread(UINT exitCode, BOOL deleteThread);

} // namespace mfc::impl
