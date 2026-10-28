#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

    // Counted objects live on the game's heap: null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    std::int16_t refs_;
    std::uint8_t flags6_;
    std::uint8_t flags7_;
};
static_assert(sizeof(CRefCounted) == 8);

// The test the network code inlines before using a counted object: set and not detached.
inline bool IsLive(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Reference-counted objects of the engine core (script values, network records and messages, AI shapes).
class CRefObject : public CRefCounted {
public:
    CRefObject() = default;
    ~CRefObject() override = default;

    virtual int OnShutdown() = 0;           // the object is still shared and about to lose its owner
    virtual CRefObject* Clone() = 0;        // the copy handed out when a kCopyOnShare object is shared

    void Release();
    // Calls OnShutdown with an extra reference held, so it cannot destroy itself meanwhile.
    int Shutdown();
};
static_assert(sizeof(CRefObject) == 8);

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
static_assert(sizeof(RefPtr) == 4);

// A typed pointer slot holding a counted object (CRefCounted): `object` gains a reference, the old one loses
// one and is deleted with the last (RefPtr::Assign on the slot).
template <typename T>
void AssignCounted(T*& slot, std::type_identity_t<T>* object) {
    T* old = slot;
    slot = object;
    if (object) ++object->refs_;
    if (old && --old->refs_ == 0) delete old;
}

// A typed counted pointer's plain release (RefPtrRelease on the slot's content): one reference dropped, the
// object deleted with the last. The pointer itself is left as it was.
template <typename T>
void ReleaseCounted(T* object) {
    if (object && --object->refs_ == 0) delete object;
}

// A counted pointer that holds its reference (a container's element): made from a pointer or copied, it takes a
// reference; destroyed, it drops one and deletes the object with the last (ReleaseCounted). Assigning takes the
// new reference before the old one is dropped. A move hands the reference over.
template <typename T>
class CountedPtr {
public:
    CountedPtr() = default;
    CountedPtr(T* object) : object_(object) {
        if (object_) ++object_->refs_;
    }
    CountedPtr(const CountedPtr& other) : CountedPtr(other.object_) {}
    CountedPtr(CountedPtr&& other) noexcept : object_(std::exchange(other.object_, nullptr)) {}
    CountedPtr& operator=(const CountedPtr& other) {
        AssignCounted(object_, other.object_);
        return *this;
    }
    CountedPtr& operator=(CountedPtr&& other) noexcept {
        std::swap(object_, other.object_);
        return *this;
    }
    ~CountedPtr() { ReleaseCounted(object_); }

    T* get() const { return object_; }
    operator T*() const { return object_; }
    T* operator->() const { return object_; }

private:
    T* object_ = nullptr;
};

// The owner's release of a typed slot (RefPtr::Destroy): a still-shared object is detached and shut down first,
// then the slot's content (reloaded: the shutdown may have changed it) is released.
template <typename T>
void DestroyOwned(T*& slot) {
    static_assert(std::is_base_of_v<CRefObject, T>);
    T* object = slot;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefCounted::kDetached;
        ++object->refs_;
        object->OnShutdown();
        --object->refs_;
    }
    ReleaseCounted(slot);
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
    ++object->refs_;
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
static_assert(sizeof(RefPtrTo<CRefObject>) == 4);

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
    ~CountedRefPtr() { ReleaseCounted(ptr); }
};
static_assert(sizeof(CountedRefPtr) == 4);

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
static_assert(sizeof(SharedRefPtr) == 4);

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

// A counted reference to a figure (CLinkMesh), the client half's wounds material or its helper preset;
// object3d_lifetime.cpp defines Assign for those. A plain store (`ref = p`) takes and drops no reference.
template <typename T>
struct CountedRef {
    // The new object gains a reference; the old one loses one and is deleted when that was the last.
    void Assign(T* value);

    operator T*() const { return object; }
    T* operator->() const { return object; }
    CountedRef& operator=(T* plain) {
        object = plain;
        return *this;
    }

    T* object;
};

