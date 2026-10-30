#pragma once

// Worker threads and events (docs/notes/platform.md). A lock is a std::recursive_mutex (a critical section is
// recursive); a thread's end is waited for with a Signal, whose calls stay on the import slots the harness traces.

namespace platform {

using ThreadProcedure = unsigned(__stdcall*)(void* parameter);

// A thread made as MFC's AfxBeginThread did: created suspended, the priority set (SetThreadPriority's values: 2 is
// the highest), then running. It ends when the procedure returns. Closing it (or destroying the object) lets go of
// the handle; the thread keeps running.
class Thread {
public:
    Thread() = default;
    ~Thread() { Close(); }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Starts `procedure(parameter)` at `priority`; false when the thread could not be made. A held handle is
    // closed first.
    bool Start(ThreadProcedure procedure, void* parameter, int priority);
    bool Started() const { return handle != nullptr; }
    void Close();
    // The handle, which the caller closes, and the object empty (for code that keeps a native handle).
    void* Release();

private:
    void* handle = nullptr;
};

// An auto-reset event (a signal from one thread to another), not signalled when made.
class Signal {
public:
    Signal() = default;
    ~Signal() { Close(); }
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    // Makes the event (a made one is closed first).
    void Create();
    bool Created() const { return handle != nullptr; }
    void Set();
    // Waits until the event is signalled (no time limit).
    void Wait();
    void Close();

private:
    void* handle = nullptr;
};

} // namespace platform
