// The SDL backend's threads and signals (platform.h, "Threads").
#include "platform/platform.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

namespace platform {

// SDL's threads; a thread sets its own priority as it starts (SDL sets only the calling thread's).

namespace {

struct ThreadStart {
    ThreadProcedure procedure;
    void* parameter;
    SDL_ThreadPriority priority;
};

int SDLCALL RunThread(void* data) {
    const ThreadStart start = *static_cast<ThreadStart*>(data);
    delete static_cast<ThreadStart*>(data);
    SDL_SetCurrentThreadPriority(start.priority);
    return static_cast<int>(start.procedure(start.parameter));
}

// SetThreadPriority's values (15 time-critical, 1 and 2 above normal, below 0 below it) as SDL's.
SDL_ThreadPriority PriorityOf(int priority) {
    if (priority >= 15) return SDL_THREAD_PRIORITY_TIME_CRITICAL;
    if (priority > 0) return SDL_THREAD_PRIORITY_HIGH;
    if (priority < 0) return SDL_THREAD_PRIORITY_LOW;
    return SDL_THREAD_PRIORITY_NORMAL;
}

// A Signal's state: set until a Wait takes it.
struct SignalState {
    SDL_Mutex* lock = SDL_CreateMutex();
    SDL_Condition* changed = SDL_CreateCondition();
    bool set = false;

    ~SignalState() {
        SDL_DestroyCondition(changed);
        SDL_DestroyMutex(lock);
    }
};

} // namespace

bool Thread::Start(ThreadProcedure procedure, void* parameter, int priority) {
    Close();
    auto* start = new ThreadStart{procedure, parameter, PriorityOf(priority)};
    SDL_Thread* thread = SDL_CreateThread(RunThread, "game", start);
    if (!thread) {
        delete start;
        return false;
    }
    handle = thread;
    return true;
}

void Thread::Close() {
    if (handle) SDL_DetachThread(static_cast<SDL_Thread*>(handle));
    handle = nullptr;
}

void Signal::Create() {
    Close();
    handle = new SignalState;
}

void Signal::Set() {
    auto* state = static_cast<SignalState*>(handle);
    SDL_LockMutex(state->lock);
    state->set = true;
    SDL_SignalCondition(state->changed);
    SDL_UnlockMutex(state->lock);
}

void Signal::Wait() {
    auto* state = static_cast<SignalState*>(handle);
    SDL_LockMutex(state->lock);
    while (!state->set) SDL_WaitCondition(state->changed, state->lock);
    state->set = false;
    SDL_UnlockMutex(state->lock);
}

void Signal::Close() {
    delete static_cast<SignalState*>(handle);
    handle = nullptr;
}

} // namespace platform
