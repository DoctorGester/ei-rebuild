#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "lib/bit_flags.h"
#include "engine/core/ref.h"

class ChunkArchive;

// The classes a ChunkArchive holds by reference: a save's index stores the id, a load creates the object from it
// (ChunkArchive::Open). Every other class saves as None (-1), which loads as no object.
enum class ArchiveClass : std::int32_t {
    None = -1,
    ClassTemplate = 1,
    PartyRecord = 2,
};

class RefCounted;
// The object an index entry's class id stands for, new with no references; null for any other id. The game's
// (players/party_manager.cpp: the class templates and the party records are its classes).
RefCounted* CreateArchived(ArchiveClass type);

// The bits of RefCounted's two flag bytes.
enum class RefFlag : std::uint8_t {
    CopyOnShare = 0x01,    // flags6: sharing makes a copy (the copy is marked too)
    Detached = 0x80,       // flags7: shut down by its owner; not shared any more
};
constexpr bool EnableBitFlags(RefFlag) { return true; }

// The root of the reference-counted classes. The network sessions and the cast spells derive from it directly,
// everything else through RefObject.
class RefCounted {
public:
    RefCounted() = default;
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
    virtual ~RefCounted() {}
    virtual void Serialize(ChunkArchive*) {}
    virtual ArchiveClass ArchiveType() const { return ArchiveClass::None; }

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the object.
    void Release() {
        if (--refs == 0) delete this;
    }

    // A new object has no references and no flags.
    std::int16_t refs = 0;
    RefFlag flags6 = {};
    RefFlag flags7 = {};
};

// The test the network code inlines before using a counted object: set and not detached.
inline bool IsLive(const RefCounted* object) { return object && !lib::Has(object->flags7, RefFlag::Detached); }

// Reference-counted objects of the engine core (script values, network records and messages, AI shapes).
class RefObject : public RefCounted {
public:
    RefObject() = default;
    ~RefObject() override = default;

    virtual void OnShutdown() {}             // the object is still shared and about to lose its owner
    // The copy handed out when a RefFlag::CopyOnShare object is shared; fatal for a class that is never shared twice.
    virtual RefObject* Clone();

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
        object->flags7 |= RefFlag::Detached;
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
    if (!IsLive(object)) return;
    if (lib::Has(object->flags6, RefFlag::CopyOnShare)) object = static_cast<T*>(object->Clone());
    slot.Assign(object);
    object->flags6 |= RefFlag::CopyOnShare;
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
