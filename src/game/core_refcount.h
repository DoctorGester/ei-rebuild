#pragma once

#include <cstdint>

// Reference-counted objects of the engine core (script values, network records and messages, AI
// shapes; vtable 0073BB58 and its descendants). Names provisional. The header and the slots below
// are what the engine's helpers use; the original code mostly inlines these helpers, the
// out-of-line copies are rewritten here.
class CRefObject {
public:
    enum : std::uint8_t {
        kCopyOnShare = 0x01,    // flags6_: sharing makes a copy (the copy is marked too)
        kDetached = 0x80,       // flags7_: shut down by its owner; not shared any more
    };

    virtual ~CRefObject();                  // 0: scalar deleting destructor (1 when the last reference goes)
    virtual void Slot1(int);                // 1: empty in the root class (RET 4)
    virtual int OnShutdown() = 0;           // 2: the object is still shared and about to lose its owner
    virtual CRefObject* Clone() = 0;        // 3: the copy handed out when a kCopyOnShare object is shared

    // 0040A3A0. Drops one reference; destroys the object at zero.
    void Release();
    // 00417DF0. Calls OnShutdown with an extra reference held, so it cannot destroy itself
    // meanwhile. Returns OnShutdown's EAX.
    int Shutdown();

    std::int16_t refs_;         // +0x04
    std::uint8_t flags6_;       // +0x06
    std::uint8_t flags7_;       // +0x07
};
static_assert(sizeof(CRefObject) == 8);

// An owning pointer to a CRefObject (one dword).
struct RefPtr {
    CRefObject* ptr;

    // 00419CD0. Points at `object` (adding a reference) and releases the previous one.
    void Assign(CRefObject* object);
    // 00418BE0. Initialises to share `object`: null for a null or detached object, a clone for a
    // copy-on-share one. The result is marked copy-on-share.
    void Share(CRefObject* object);
    // 00413270. The owner's release: a still-shared object is detached and shut down first. The
    // pointer itself is left as it was.
    void Destroy();
};
static_assert(sizeof(RefPtr) == 4);
