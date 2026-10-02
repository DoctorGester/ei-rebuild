#pragma once

#include <cstdint>

// Reference-counted objects of the engine core (script values and the containers around them;
// vtables 0073BB58, 0073BD90, 0073BDB0, ...). Names provisional. Only the header is known:
//   slot 0: scalar deleting destructor (called with 1 when the last reference goes)
//   slot 2: shutdown, called while the object is still shared and about to lose its owner
//   slot 3: clone, used when an object marked kCopyOnShare is shared
// The original code mostly inlines these helpers; the out-of-line copies are rewritten here.
class CRefObject {
public:
    enum : std::uint8_t {
        kCopyOnShare = 0x01,    // flags6_: sharing makes a copy (the copy is marked too)
        kDetached = 0x80,       // flags7_: shut down by its owner; not shared any more
    };

    // 0040A3A0. Drops one reference; destroys the object at zero.
    void Release();
    // 00417DF0. Calls slot 2 with an extra reference held, so it cannot destroy itself meanwhile.
    // Returns slot 2's EAX.
    int Shutdown();

    void* vtable_;
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
