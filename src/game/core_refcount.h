#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "ref.h"

class ChunkArchive;

// The root of the reference-counted classes. The network sessions and the cast spells derive from it directly,
// everything else through CRefObject.
class CRefCounted {
public:

    enum : std::uint8_t {
        kCopyOnShare = 0x01,    // flags6_: sharing makes a copy (the copy is marked too)
        kDetached = 0x80,       // flags7_: shut down by its owner; not shared any more
    };

    // The count and the flags are the creator's to set (a class rebuilt in place keeps them).
    CRefCounted() = default;
    CRefCounted(const CRefCounted&) = delete;
    CRefCounted& operator=(const CRefCounted&) = delete;
    virtual ~CRefCounted() {}
    virtual void Serialize(ChunkArchive*) {}

    void AddRef() { ++refs_; }
    // Drops a reference; the last one deletes the object.
    void Release() {
        if (--refs_ == 0) delete this;
    }

    std::int16_t refs_;
    std::uint8_t flags6_;
    std::uint8_t flags7_;
};

// The test the network code inlines before using a counted object: set and not detached.
inline bool IsLive(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Reference-counted objects of the engine core (script values, network records and messages, AI shapes).
class CRefObject : public CRefCounted {
public:
    CRefObject() = default;
    ~CRefObject() override = default;

    virtual int OnShutdown() = 0;           // the object is still shared and about to lose its owner
    virtual CRefObject* Clone() = 0;        // the copy handed out when a kCopyOnShare object is shared

    // Calls OnShutdown with an extra reference held, so it cannot destroy itself meanwhile.
    int Shutdown();
};

struct RefPtr {
    RefPtr* FromRawCounted(CRefObject* object);
    RefPtr* AssignObject(CRefObject* object);
    CRefObject* ptr;

    void Assign(CRefObject* object);
    // Initialises to share `object`: null for a null or detached object, a clone for a
    // copy-on-share one. The result is marked copy-on-share.
    void Share(CRefObject* object);
    // Share, returning this.
    RefPtr* ShareReturn(CRefObject* object);
    // The owner's release: a still-shared object is detached and shut down first. The
    // pointer itself is left as it was.
    void Destroy();
    // The AI mode list's element assignment: unless already `object`, Destroy, then Share(object).
    RefPtr* ShareAssign(CRefObject* object);
};

// A typed pointer slot holding a counted object: `object` gains a reference, the old one loses one. Only the
// interface's item view still stores into a raw slot with it; everything else holds a Ref.
template <typename T>
void AssignCounted(T*& slot, std::type_identity_t<T>* object) {
    T* old = slot;
    slot = object;
    if (object) object->AddRef();
    if (old) old->Release();
}

// The name ui_briefing.h gives HeldRef until the interface track's files are converted.
template <typename T>
using CountedPtr = HeldRef<T>;

// The owner's release of a typed slot (RefPtr::Destroy): a still-shared object is detached and shut down first,
// then the slot's content (reloaded: the shutdown may have changed it) is released.
template <typename T>
void DestroyOwned(T*& slot) {
    static_assert(std::is_base_of_v<CRefObject, T>);
    T* object = slot;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefCounted::kDetached;
        object->AddRef();
        object->OnShutdown();
        --object->refs_;
    }
    if (T* reloaded = slot) reloaded->Release();
}

// The owner's share into a typed slot (RefPtr::Share): null for a null or detached object, a clone for a
// copy-on-share one; the result is marked copy-on-share.
template <typename T>
void ShareOwned(T*& slot, T* object) {
    static_assert(std::is_base_of_v<CRefObject, T>);
    if (!object || (object->flags7_ & CRefCounted::kDetached)) {
        slot = nullptr;
        return;
    }
    if (object->flags6_ & CRefCounted::kCopyOnShare) object = static_cast<T*>(object->Clone());
    slot = object;
    object->AddRef();
    object->flags6_ |= CRefCounted::kCopyOnShare;
}

// RefPtr::ShareAssign on a typed slot: unless it already holds `object`, DestroyOwned, then ShareOwned.
template <typename T>
void ShareAssignOwned(T*& slot, T* object) {
    if (object == slot) return;
    DestroyOwned(slot);
    ShareOwned(slot, object);
}

// A RefPtr field whose object is always a T (a network message of one class): RefPtr's layout
// and operations, the object read typed.
template <typename T>
struct RefPtrTo : RefPtr {
    T* Get() const { return static_cast<T*>(ptr); }
};

// A RefPtr that holds a plain reference (a container's element): a copy takes one (FromRawCounted), the destructor
// drops it (the plain release), an assignment takes the new reference before the old one is dropped (Assign). A
// move hands the reference over.
struct CountedRefPtr : RefPtr {
    CountedRefPtr() : RefPtr{nullptr} {}
    explicit CountedRefPtr(CRefObject* object) : RefPtr{nullptr} { FromRawCounted(object); }
    CountedRefPtr(const CountedRefPtr& other) : RefPtr{nullptr} { FromRawCounted(other.ptr); }
    CountedRefPtr(CountedRefPtr&& other) noexcept : RefPtr{std::exchange(other.ptr, nullptr)} {}
    CountedRefPtr& operator=(const CountedRefPtr& other) {
        Assign(other.ptr);
        return *this;
    }
    CountedRefPtr& operator=(CountedRefPtr&& other) noexcept {
        std::swap(ptr, other.ptr);
        return *this;
    }
    ~CountedRefPtr() {
        if (ptr) ptr->Release();
    }
};

// A RefPtr that owns its share (a container's element): a copy shares the object (Share: a clone of a
// copy-on-share one), the destructor is the owner's release (Destroy), an assignment ShareAssign. A move hands
// the share over.
struct SharedRefPtr : RefPtr {
    SharedRefPtr() : RefPtr{nullptr} {}
    explicit SharedRefPtr(CRefObject* object) : RefPtr{nullptr} { Share(object); }
    SharedRefPtr(const SharedRefPtr& other) : RefPtr{nullptr} { Share(other.ptr); }
    SharedRefPtr(SharedRefPtr&& other) noexcept : RefPtr{std::exchange(other.ptr, nullptr)} {}
    SharedRefPtr& operator=(const SharedRefPtr& other) {
        ShareAssign(other.ptr);
        return *this;
    }
    SharedRefPtr& operator=(SharedRefPtr&& other) noexcept {
        std::swap(ptr, other.ptr);
        return *this;
    }
    ~SharedRefPtr() { Destroy(); }
};

// A SharedRefPtr / CountedRefPtr whose object is always a T (the element of a list of one network message
// class): the same copies and releases, the object read typed.
template <typename T>
struct SharedRefTo : SharedRefPtr {
    using SharedRefPtr::SharedRefPtr;
    T* Get() const { return static_cast<T*>(ptr); }
};
template <typename T>
struct CountedRefTo : CountedRefPtr {
    using CountedRefPtr::CountedRefPtr;
    T* Get() const { return static_cast<T*>(ptr); }
};
