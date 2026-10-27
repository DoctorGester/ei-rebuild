#pragma once

#include <cstddef>
#include <cstdint>

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
    RefPtr* DeleteScalar(unsigned flags);
    RefPtr* AssignCounted(const RefPtr* other);
    RefPtr* CopyPlain(const RefPtr* other);
    RefPtr* CopyShared(const RefPtr* other);
    RefPtr* DeleteReleased(unsigned flags);
    RefPtr* DeleteDestroyed(unsigned flags);
    RefPtr* FromRawCounted(CRefObject* object);
    // counted_ptr<T>'s copy constructor.
    RefPtr* CopyCounted(const RefPtr* from);
    RefPtr* AssignHolder(const RefPtr* other);
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

// A RefPtr field whose object is always a T (a network message of one class): RefPtr's layout
// and operations, the object read typed.
template <typename T>
struct RefPtrTo : RefPtr {
    T* Get() const { return static_cast<T*>(ptr); }
};
static_assert(sizeof(RefPtrTo<CRefObject>) == 4);

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

// `self` holds `object` without a reference; returns self.
RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);
void __fastcall RefPtrClear(RefPtr* self);
