#pragma once

#include <cstddef>
#include <utility>

// The game's counted pointers. T counts its own references: AddRef() takes one, Release() drops one and frees the
// object with the last (CEngineObject through its owner, CRefCounted, the figures, the device materials, the render
// presets and helper). A site that keeps a raw pointer counts by hand with the same two members.

// A counted pointer: Assign and Reset count, reads see the object, and its destructor drops the reference it
// holds. There is no plain store.
template <typename T>
class Ref {
public:
    Ref() = default;
    Ref(std::nullptr_t) {}
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { Reset(); }

    // Holds `object`: it gains a reference, then the old one loses its own. Nothing when it is held already.
    void Assign(T* newObject) {
        if (newObject == object) return;
        if (newObject) newObject->AddRef();
        if (T* old = std::exchange(object, newObject)) old->Release();
    }
    // Empties the pointer, then drops the reference it held.
    void Reset() {
        if (T* old = std::exchange(object, nullptr)) old->Release();
    }
    // Empties the pointer and returns the object: its reference goes with it.
    T* Take() { return std::exchange(object, nullptr); }

    T* Get() const { return object; }
    operator T*() const { return object; }
    T* operator->() const { return object; }

protected:
    T* object = nullptr;
};

// A Ref that can be copied and moved (a container's element): made from a pointer or copied it takes a reference.
// An assignment takes the new reference before the old one is dropped; a move hands the reference over.
template <typename T>
class HeldRef : public Ref<T> {
public:
    HeldRef() = default;
    HeldRef(T* object) { Ref<T>::Assign(object); }
    HeldRef(const HeldRef& other) : HeldRef(other.object) {}
    HeldRef(HeldRef&& other) noexcept { this->object = other.Take(); }
    HeldRef& operator=(HeldRef other) noexcept {
        std::swap(this->object, other.object);
        return *this;
    }
};
