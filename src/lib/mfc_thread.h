#pragma once

// MFC 4.2's worker threads as the game starts them (mfc_thread.cpp, ticket S16): AfxBeginThread with
// a thread procedure (the sound stream, the MIDI player and the cursor) and AfxEndThread, which the
// procedures call at their end. The CWinThread constructor and its CommonConstruct are registered in
// both builds; AfxBeginThread and AfxEndThread in the image only (the hook build keeps the original's
// thread state, which they keep the current thread in). The image's threads come from the host CRT's
// _beginthreadex and need no MFC thread state: the procedures never pump messages or open dialogs, so
// AfxInitThread's message filter hook and AfxTermThread's temporary maps are left out.

#include "mfc_window.h"

#include <windows.h>

#include <cstddef>

namespace mfc::impl {

using ThreadProc = UINT(__cdecl*)(void* parameter);

// CWinThread (0x68 bytes, vtable 007466E0).
struct WinThread {
    CCmdTarget base;
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

    static void* Vtable() { return reinterpret_cast<void*>(0x007466E0); }

    // 0070C259 (thiscall, RET 8, returns this): CCmdTarget's constructor, the vtable, the procedure
    // and its parameter, then CommonConstruct.
    WinThread* Construct(ThreadProc procedure, void* parameter);
    // 0070C2F6 (thiscall): no windows, no thread yet, the message state cleared, the cursor position
    // (GetCursorPos), no filter or OLE hook, deleted when its thread ends.
    void CommonConstruct();
    // 00702596 Delete (vtable slot 0x1E): the deleting destructor when autoDelete is set. The
    // destructor (0070E132) closes the thread handle and stops being the current thread.
    void Delete();
};
static_assert(sizeof(WinThread) == 0x68);
static_assert(offsetof(WinThread, thread) == 0x28 && offsetof(WinThread, threadParams) == 0x4C);
static_assert(offsetof(WinThread, cursorLast) == 0x5C && offsetof(WinThread, msgLast) == 0x64);

// 007022A0 AfxBeginThread (stdcall): a new CWinThread for `procedure`, its thread created suspended
// (CreateThread 007024B4: started, then held until the creator has its handle), the priority set,
// then resumed unless `flags` has CREATE_SUSPENDED. Null when the thread cannot be created. Image only.
WinThread* __stdcall AfxBeginThread(ThreadProc procedure, void* parameter, int priority, UINT stackSize,
                                    DWORD flags, LPSECURITY_ATTRIBUTES security);
// 00702380 AfxEndThread (stdcall; does not return): the current thread's OLE hook, then its object
// deleted when `deleteThread` (and autoDelete) is set, then _endthreadex(exitCode). Image only.
void __stdcall AfxEndThread(UINT exitCode, BOOL deleteThread);

} // namespace mfc::impl
