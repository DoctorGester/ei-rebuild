#pragma once

// A global of game.exe's whose constructor and destructor run where game.exe runs them: the constructor from its
// static initialiser in the list's order,
// the destructor from the exit function that initialiser registers with the game's atexit.
// docs/notes/standalone.md, "The static initialisers' objects".
// The storage starts zero (constant initialisation: `constinit lib::StaticObject<T> g_x = /*address*/ {};`, compared
// with game.exe's bytes by tools/named_data.py) and our own CRT neither constructs nor destroys it: T's constructor
// may read game data the image sets up later, and its destructor must run in the game's exit order, on the game's
// heap, not at our CRT's exit.

#include <cstdint>
#include <new>
#include <utility>

namespace lib {

// The once-only guard (bit 0) that game.exe's compiler gave the function-local statics with an empty destructor: 175
// static initialisers (the generated static_init.cpp's `d.Once(lib::g_emptyStaticGuard, 1)`) and CSysInfo's
// (sys_info.cpp) set it and register the empty exit function; nothing reads it but these tests.
inline constinit std::uint8_t g_emptyStaticGuard = /*007CAEE4*/ {};

template <class T>
class StaticObject {
public:
    constexpr StaticObject() = default;
    StaticObject(const StaticObject&) = delete;
    StaticObject& operator=(const StaticObject&) = delete;

    template <class... A>
    T& Construct(A&&... arguments) {
        return *new (bytes_) T(std::forward<A>(arguments)...);
    }
    void Destroy() { Get().~T(); }

    T& Get() { return *std::launder(reinterpret_cast<T*>(bytes_)); }
    const T& Get() const { return *std::launder(reinterpret_cast<const T*>(bytes_)); }
    T& operator*() { return Get(); }
    const T& operator*() const { return Get(); }
    T* operator->() { return &Get(); }
    const T* operator->() const { return &Get(); }

private:
    alignas(T) unsigned char bytes_[sizeof(T)] = {};
};

// A T inside its owner (a local or a member) whose constructor and destructor the owner calls itself, where
// game.exe does (an archive on a loader's stack or in a window, built by its constructor's address and
// destroyed through Rebirth's patched destructor): a union member, so the compiler runs neither.
template <class T>
union InPlace {
    InPlace() {}
    ~InPlace() {}
    InPlace(const InPlace&) = delete;
    InPlace& operator=(const InPlace&) = delete;
    T* Get() { return &object; }
    T* operator->() { return &object; }
    T object;
};

// As InPlace for an abstract T (pure virtual slots: neither a member nor a union member can have its
// type): the object's bytes inside its owner, built and destroyed by the owner's code through the
// class's constructor and destructor bodies; Get is the one typed view of them.
template <class T>
class AbstractInPlace {
public:
    T* Get() { return std::launder(reinterpret_cast<T*>(bytes_)); }
    const T* Get() const { return std::launder(reinterpret_cast<const T*>(bytes_)); }
    T* operator->() { return Get(); }
    const T* operator->() const { return Get(); }

private:
    alignas(T) unsigned char bytes_[sizeof(T)];
};

} // namespace lib
