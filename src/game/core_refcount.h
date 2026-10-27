#pragma once

#include <cstddef>
#include <cstdint>

class ChunkArchive;

// The root of the reference-counted classes (game.exe's table 0073BB68): the count and the flags, slot 0 the
// scalar deleting destructor, slot 1 Serialize (empty, RET 4). The network sessions (net_classes.h) and the cast
// spells (spell.h) derive from it directly; everything else through CRefObject below.
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
    // 0: the scalar deleting destructor (1 when the last reference goes). game.exe's out-of-line bodies: 00412720 and
    // its copies, deleting 00412730 (core_streams.cpp).
    virtual ~CRefCounted() {}
    virtual void Serialize(ChunkArchive*) {}   // 1: empty in the root class (00412580, RET 4); chunk_archive.h

    // Counted objects live on the game's heap (core_refcount.cpp): null without memory, and then
    // no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    std::int16_t refs_;         // +0x04
    std::uint8_t flags6_;       // +0x06
    std::uint8_t flags7_;       // +0x07
};
static_assert(sizeof(CRefCounted) == 8);

// The test the network code inlines before using a counted object: set and not detached.
inline bool IsLive(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Reference-counted objects of the engine core (script values, network records and messages, AI
// shapes; vtable 0073BB58 and its descendants). Names provisional. The header and the slots below
// are what the engine's helpers use; the original code mostly inlines these helpers, the
// out-of-line copies are rewritten here.
class CRefObject : public CRefCounted {
public:
    // game.exe's table 0073BB58 (2 / 3 `_purecall`): its out-of-line constructor 00419720; the deleting destructor
    // 00412700 and its copies (core_streams.cpp).
    CRefObject() = default;
    ~CRefObject() override = default;

    virtual int OnShutdown() = 0;           // 2: the object is still shared and about to lose its owner
    virtual CRefObject* Clone() = 0;        // 3: the copy handed out when a kCopyOnShare object is shared

    // Drops one reference; destroys the object at zero.
    void Release();
    // Calls OnShutdown with an extra reference held, so it cannot destroy itself
    // meanwhile. Returns OnShutdown's EAX.
    int Shutdown();
};
static_assert(sizeof(CRefObject) == 8);

// An owning pointer to a CRefObject (one dword).
struct RefPtr {
    // Body of the slot at 004AB960 (registered by member pointer).
    RefPtr* DeleteScalar(unsigned flags);
    RefPtr* AssignCounted(const RefPtr* other);
    RefPtr* CopyPlain(const RefPtr* other);
    RefPtr* CopyShared(const RefPtr* other);
    RefPtr* DeleteReleased(unsigned flags);
    RefPtr* DeleteDestroyed(unsigned flags);
    RefPtr* FromRawCounted(CRefObject* object);
    // 004A5D50 / 004ABE30 (RET 4): counted_ptr<T>'s copy constructor: from's object, one more
    // reference. Returns this.
    RefPtr* CopyCounted(const RefPtr* from);
    RefPtr* AssignHolder(const RefPtr* other);
    RefPtr* AssignObject(CRefObject* object);
    CRefObject* ptr;

    // Points at `object` (adding a reference) and releases the previous one.
    void Assign(CRefObject* object);
    // Initialises to share `object`: null for a null or detached object, a clone for a
    // copy-on-share one. The result is marked copy-on-share.
    void Share(CRefObject* object);
    // 004292F0 and eight identical copies (refptr_copies.cpp). The same, returning this.
    RefPtr* ShareReturn(CRefObject* object);
    // The owner's release: a still-shared object is detached and shut down first. The
    // pointer itself is left as it was.
    void Destroy();
    // 005D9D40 (RET 4; the AI mode list's element assignment). Unless it already points at
    // `object`: Destroy, then Share(object). Returns this.
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

// A counted reference: the pointer slot is the `this` of 00519580 (the 3D object's figure, a
// CLinkMesh) and of its byte-identical copies 0051E570 (the client half's wounds material) and
// 0051E4F0 (that material's helper preset), both called by 0051C3D0. T's count is at +0x04 and its
// slot 0 deletes it; object3d_lifetime.cpp defines Assign for those three types. Reads see the
// object (the conversion and ->); a plain store (`ref = p`) neither takes nor drops a reference.
template <typename T>
struct CountedRef {
    // object = `value`: the new one gains a reference, the old one loses one and is deleted (its
    // slot 0 with 1) when that was the last.
    void Assign(T* value);

    operator T*() const { return object; }
    T* operator->() const { return object; }
    CountedRef& operator=(T* plain) {
        object = plain;
        return *this;
    }

    T* object;
};

// refptr_copies.cpp (ECX the holder, EDX the object). 00427D50 and its 15 identical copies: `self`
// holds `object` without a reference; returns self. 00421360 and its identical copies: clears it.
RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);
void __fastcall RefPtrClear(RefPtr* self);
