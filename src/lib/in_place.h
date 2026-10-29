#pragma once

// A T inside its owner (a local or a member) whose constructor and destructor the owner calls itself, where
// game.exe does (an archive on a loader's stack or in a window, built by its constructor's address and
// destroyed through Rebirth's patched destructor): a union member, so the compiler runs neither.

namespace lib {

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

} // namespace lib
