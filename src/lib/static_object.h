#pragma once

// A global of game.exe's whose constructor and destructor run where the original runs them: the constructor from its
// static initialiser in the list's order (ported by hand on the name and registered at the initialiser's address),
// the destructor from the exit function that initialiser registers with the game's atexit (ported the same way).
// docs/notes/standalone.md, "The static initialisers' objects" (C18k).
//
// The storage starts zero (constant initialisation: `constinit lib::StaticObject<T> g_x = /*address*/ {};`, compared
// with game.exe's bytes by tools/named_data.py) and our own CRT neither constructs nor destroys it: T's constructor
// may read game data the image sets up later, and its destructor must run in the game's exit order, on the game's
// heap, not at our CRT's exit.

#include <new>
#include <utility>

namespace lib {

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

} // namespace lib
