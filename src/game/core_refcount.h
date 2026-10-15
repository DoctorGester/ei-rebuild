#pragma once

#include <cstdint>

class ChunkArchive;

// The root of the reference-counted classes (vtable 0073BB68): the count and the flags, slot 0 the
// scalar deleting destructor, slot 1 Serialize (empty, RET 4). The network sessions (net_classes.h)
// derive from it directly; everything else through CRefObject below.
class CRefCounted {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BB68);

    enum : std::uint8_t {
        kCopyOnShare = 0x01,    // flags6_: sharing makes a copy (the copy is marked too)
        kDetached = 0x80,       // flags7_: shut down by its owner; not shared any more
    };

    virtual ~CRefCounted();                 // 0: scalar deleting destructor (1 when the last reference goes)
    virtual void Serialize(ChunkArchive* ar);   // 1: empty in the root class (RET 4); chunk_archive.h

    std::int16_t refs_;         // +0x04
    std::uint8_t flags6_;       // +0x06
    std::uint8_t flags7_;       // +0x07
};
static_assert(sizeof(CRefCounted) == 8);

// Reference-counted objects of the engine core (script values, network records and messages, AI
// shapes; vtable 0073BB58 and its descendants). Names provisional. The header and the slots below
// are what the engine's helpers use; the original code mostly inlines these helpers, the
// out-of-line copies are rewritten here.
class CRefObject : public CRefCounted {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BB58);

    virtual int OnShutdown() = 0;           // 2: the object is still shared and about to lose its owner
    virtual CRefObject* Clone() = 0;        // 3: the copy handed out when a kCopyOnShare object is shared

    // 0040A3A0. Drops one reference; destroys the object at zero.
    void Release();
    // 00417DF0. Calls OnShutdown with an extra reference held, so it cannot destroy itself
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
    // 004A5D50 / 004ABE30 (RET 4; S16a): counted_ptr<T>'s copy constructor: from's object, one more
    // reference. Returns this.
    RefPtr* CopyCounted(const RefPtr* from);
    RefPtr* AssignHolder(const RefPtr* other);
    RefPtr* AssignObject(CRefObject* object);
    CRefObject* ptr;

    // 00419CD0. Points at `object` (adding a reference) and releases the previous one.
    void Assign(CRefObject* object);
    // 00418BE0. Initialises to share `object`: null for a null or detached object, a clone for a
    // copy-on-share one. The result is marked copy-on-share.
    void Share(CRefObject* object);
    // 004292F0 and eight identical copies (refptr_copies.cpp). The same, returning this.
    RefPtr* ShareReturn(CRefObject* object);
    // 00413270. The owner's release: a still-shared object is detached and shut down first. The
    // pointer itself is left as it was.
    void Destroy();
    // 005D9D40 (RET 4; the AI mode list's element assignment). Unless it already points at
    // `object`: Destroy, then Share(object). Returns this.
    RefPtr* ShareAssign(CRefObject* object);
};
static_assert(sizeof(RefPtr) == 4);
