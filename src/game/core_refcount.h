#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "ref.h"

class ChunkArchive;

// The root of the reference-counted classes. The network sessions and the cast spells derive from it directly,
// everything else through RefObject.
class RefCounted {
public:

    enum : std::uint8_t {
        kCopyOnShare = 0x01,    // flags6: sharing makes a copy (the copy is marked too)
        kDetached = 0x80,       // flags7: shut down by its owner; not shared any more
    };

    // The count and the flags are the creator's to set (a class rebuilt in place keeps them).
    RefCounted() = default;
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
    virtual ~RefCounted() {}
    virtual void Serialize(ChunkArchive*) {}

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the object.
    void Release() {
        if (--refs == 0) delete this;
    }

    std::int16_t refs;
    std::uint8_t flags6;
    std::uint8_t flags7;
};

// The test the network code inlines before using a counted object: set and not detached.
inline bool IsLive(const RefCounted* object) { return object && !(object->flags7 & RefCounted::kDetached); }

// Reference-counted objects of the engine core (script values, network records and messages, AI shapes).
class RefObject : public RefCounted {
public:
    RefObject() = default;
    ~RefObject() override = default;

    virtual void OnShutdown() = 0;           // the object is still shared and about to lose its owner
    virtual RefObject* Clone() = 0;        // the copy handed out when a kCopyOnShare object is shared

    // Calls OnShutdown with an extra reference held, so it cannot destroy itself meanwhile.
    void Shutdown();
};

// Ownership of a shared object. An owner holds its own share of an object others may hold too: sharing a
// copy-on-share object hands out a clone, and the owner's release of a still-shared object detaches it and shuts
// it down first (the others see it is gone with IsLive). The network messages, sessions, server entries and links
// and the AI's modes and unit shapes are held this way: a field as a Ref (released with DestroyOwned), a container
// element as a SharedRef.

// The owner's release: a still-shared object is detached and shut down first, then the slot's content (reloaded:
// the shutdown may have changed it) is released and the slot emptied.
template <typename T>
void DestroyOwned(Ref<T>& slot) {
    static_assert(std::is_base_of_v<RefObject, T>);
    T* object = slot;
    if (!object) return;
    if (object->refs != 1) {
        object->flags7 |= RefCounted::kDetached;
        object->AddRef();
        object->OnShutdown();
        --object->refs;
    }
    slot.Reset();
}

// The owner's share into an empty slot: nothing for a null or detached object, a clone for a copy-on-share one;
// the object held is marked copy-on-share.
template <typename T>
void ShareOwned(Ref<T>& slot, std::type_identity_t<T>* object) {
    static_assert(std::is_base_of_v<RefObject, T>);
    if (!object || (object->flags7 & RefCounted::kDetached)) return;
    if (object->flags6 & RefCounted::kCopyOnShare) object = static_cast<T*>(object->Clone());
    slot.Assign(object);
    object->flags6 |= RefCounted::kCopyOnShare;
}

// Unless the slot already holds `object`: DestroyOwned, then ShareOwned.
template <typename T>
void ShareAssignOwned(Ref<T>& slot, std::type_identity_t<T>* object) {
    if (object == slot.Get()) return;
    DestroyOwned(slot);
    ShareOwned(slot, object);
}

// A Ref that owns its share (a container's element): made from a pointer or copied it shares the object
// (ShareOwned), an assignment ShareAssignOwned, and its destructor is the owner's release (DestroyOwned). A move
// hands the share over.
template <typename T>
class SharedRef : public Ref<T> {
public:
    SharedRef() = default;
    explicit SharedRef(T* object) { ShareOwned(*this, object); }
    SharedRef(const SharedRef& other) { ShareOwned(*this, other.Get()); }
    SharedRef(SharedRef&& other) noexcept { this->object = other.Take(); }
    SharedRef& operator=(const SharedRef& other) {
        ShareAssignOwned(*this, other.Get());
        return *this;
    }
    SharedRef& operator=(SharedRef&& other) noexcept {
        std::swap(this->object, other.object);
        return *this;
    }
    ~SharedRef() { DestroyOwned(*this); }
};
