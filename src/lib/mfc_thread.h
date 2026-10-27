#pragma once

// MFC 4.2's worker threads as the game starts them (mfc_thread.cpp): AfxBeginThread with
// a thread procedure (the sound stream, the MIDI player and the cursor) and AfxEndThread, which the
// procedures call at their end. The CWinThread constructor and its CommonConstruct are registered in
// both builds; AfxBeginThread and AfxEndThread in the image only (the hook build keeps the original's
// thread state, which they keep the current thread in). The image's threads come from the host CRT's
// _beginthreadex and need no MFC thread state: the procedures never pump messages or open dialogs, so
// AfxInitThread's message filter hook and AfxTermThread's temporary maps are left out.

#include "mfc_window.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstddef>

namespace mfc::impl {

using ThreadProc = UINT(__cdecl*)(void* parameter);

// CWinThread (0x68 bytes;: CCmdTarget's 20 slots, then the thread's; the destructor and
// Delete ported, the message loop's unported: the game's threads only run their procedure, never InitInstance / Run).
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

    void* mainWnd;                  // +0x1C m_pMainWnd
    void* activeWnd;                // +0x20 m_pActiveWnd
    BOOL autoDelete;                // +0x24 m_bAutoDelete
    HANDLE thread;                  // +0x28 m_hThread
    DWORD threadId;                 // +0x2C m_nThreadID
    MSG msgCur;                     // +0x30 m_msgCur
    void* threadParams;             // +0x4C m_pThreadParams
    ThreadProc threadProc;          // +0x50 m_pfnThreadProc
    void(__cdecl* oleTermOrFreeLib)(BOOL, BOOL);   // +0x54
    void* messageFilter;            // +0x58 m_pMessageFilter
    POINT cursorLast;               // +0x5C m_ptCursorLast
    UINT msgLast;                   // +0x64 m_nMsgLast
};
static_assert(sizeof(WinThread) == 0x68);
static_assert(offsetof(WinThread, thread) == 0x28 && offsetof(WinThread, threadParams) == 0x4C);
static_assert(offsetof(WinThread, cursorLast) == 0x5C && offsetof(WinThread, msgLast) == 0x64);

// AfxBeginThread: a new CWinThread for `procedure`, its thread created suspended
// (CreateThread: started, then held until the creator has its handle), the priority set,
// then resumed unless `flags` has CREATE_SUSPENDED. Null when the thread cannot be created. Image only.
WinThread* __stdcall AfxBeginThread(ThreadProc procedure, void* parameter, int priority, UINT stackSize,
                                    DWORD flags, LPSECURITY_ATTRIBUTES security);
// AfxEndThread (does not return): the current thread's OLE hook, then its object
// deleted when `deleteThread` (and autoDelete) is set, then _endthreadex(exitCode). Image only.
void __stdcall AfxEndThread(UINT exitCode, BOOL deleteThread);

} // namespace mfc::impl
