// Threads and events on Windows (platform/thread.h). Through game.exe's import slots, as the game called them.
#include "platform/thread.h"

#include "lib/gameapi.h"

#include <process.h>
#include <windows.h>

namespace platform {

bool Thread::Start(ThreadProcedure procedure, void* parameter, int priority) {
    Close();
    unsigned id = 0;
    HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, procedure, parameter, CREATE_SUSPENDED, &id));
    if (!thread) return false;
    gameapi::SetThreadPriority(thread, priority);
    gameapi::ResumeThread(thread);
    handle = thread;
    return true;
}

void Thread::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

void* Thread::Release() {
    void* released = handle;
    handle = nullptr;
    return released;
}

void Signal::Create() {
    Close();
    handle = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

void Signal::Set() { gameapi::SetEvent(static_cast<HANDLE>(handle)); }

void Signal::Wait() { gameapi::WaitForSingleObject(static_cast<HANDLE>(handle), INFINITE); }

void Signal::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

} // namespace platform
