#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

// `new (kZeroed) T(...)`: T constructed on a zeroed block, for the engine objects whose constructors leave fields
// unset (the two factories' server and map objects, the lights, the particle sources): a determinism fix the
// standalone keeps; the harness zeroes the original's at the same calls (tests/verify/harness_leftovers.h).
// `delete` frees them as any other.
struct ZeroFill {};
inline constexpr ZeroFill kZeroed{};
inline void* operator new(std::size_t size, ZeroFill) { return std::memset(::operator new(size), 0, size); }
inline void operator delete(void* block, ZeroFill) noexcept { ::operator delete(block); }

// windows.h's GetClassName macro would rename the slot in files that include windows.h first; the files that
// override it undefine it too.
#undef GetClassName

class CBaseManager;
class CManagerContext;
class CStream;
class CWorld;
class CWorldClient;
class CWorldServer;

// Base of the engine's objects and managers. No constructor: the derived classes' constructors call ClearFields.
class CEngineObject {
public:

    virtual ~CEngineObject();
    virtual void CopyFrom(const CEngineObject& other); // flags_, type_, prototypeId_
    virtual int Create() = 0;
    virtual void Destroy() = 0;
    virtual void Release();                            // refcount; the owner releases it at 0
    virtual void ClearState();
    virtual int RunTick();
    virtual const char* GetName();                     // "noname"
    virtual const char* GetDescription();              // "nodesc"
    virtual void Load(CStream* stream);
    virtual void Save(CStream* stream);
    virtual const char* GetClassName() = 0;
    virtual int GetClassId() = 0;

    CManagerContext* ManagerContext() const;
    // The manager context as the world, for the world's objects (a party's items have the party instead).
    // Templates so that the world's classes need to be complete only where they are used.
    template <class World = CWorld> World* WorldContext() const { return static_cast<World*>(ManagerContext()); }
    template <class World = CWorldServer> World* ServerWorld() const { return WorldContext<World>(); }
    template <class World = CWorldClient> World* ClientWorld() const { return WorldContext<World>(); }
    // The world the object belongs to, or null without a context.
    CWorld* World() const;
    CBaseManager* SetOwner(CBaseManager* owner);
    void ClearFields();


    int refCount_;
    std::uint32_t flags_;
    int id_;                // -1 until a manager registers the object
    int type_;              // class-specific kind (texture usage for CTexture)
    CBaseManager* owner_;
    // The database prototype: items pack its index (high word) with the material (low word), spells keep the
    // index in the high word, units the plain index.
    std::uint32_t prototypeId_;
};
static_assert(sizeof(CEngineObject) == 0x1C);

// A counted engine object pointer as the game's helpers take it. Reads see the object; Assign counts, a plain
// store (`held = p`) does not.
template <typename T>
struct HeldObject {
    void Assign(T* value) {
        CEngineObject* previous = object;
        object = value;
        if (CEngineObject* taken = value) ++taken->refCount_;
        if (previous) previous->Release();
    }
    HeldObject* Construct(T* const* value) {
        object = *value;
        if (CEngineObject* taken = object) ++taken->refCount_;
        return this;
    }
    // A counted pointer kept in a container of plain pointers seen as the holder it is.
    static HeldObject* Of(T** pointer) { return reinterpret_cast<HeldObject*>(pointer); }

    operator T*() const { return object; }
    T* operator->() const { return object; }
    HeldObject& operator=(T* plain) {
        object = plain;
        return *this;
    }

    T* object;
};
using HeldEngineObject = HeldObject<CEngineObject>;
static_assert(sizeof(HeldEngineObject) == 4);

// A counted engine object pointer that holds its reference (a container's element): made from a pointer or
// copied, it takes one; destroyed, it drops it (Release). A move hands the reference over.
template <typename T>
class HeldPtr {
public:
    HeldPtr() = default;
    HeldPtr(T* object) : object_(object) {
        if (object_) ++object_->refCount_;
    }
    HeldPtr(const HeldPtr& other) : HeldPtr(other.object_) {}
    HeldPtr(HeldPtr&& other) noexcept : object_(other.object_) { other.object_ = nullptr; }
    HeldPtr& operator=(HeldPtr other) noexcept {
        T* old = object_;
        object_ = other.object_;
        other.object_ = old;
        return *this;
    }
    ~HeldPtr() {
        if (object_) object_->Release();
    }

    T* get() const { return object_; }
    // The pointer, no longer held here: its reference goes with it.
    T* release() {
        T* object = object_;
        object_ = nullptr;
        return object;
    }
    operator T*() const { return object_; }
    T* operator->() const { return object_; }

private:
    T* object_ = nullptr;
};

// A counted reference (released through Release). The AI's reference fields and the effects' figures hold one;
// Assign / Reset count, a plain store (`ref = p`) does not. The bodies are counted_ref.cpp's.
template <typename T>
struct AIRefTo {
    void Reset();
    void Assign(T* newObject);
    // Read / write the object id (-1: none); Load looks it up in `world`.
    void Load(CStream* stream, CWorld* world);
    void Save(CStream* stream);
    // A counted object pointer kept in a container of plain pointers seen as the reference it is.
    static AIRefTo* Of(T** slot) { return reinterpret_cast<AIRefTo*>(slot); }

    operator T*() const { return object; }
    T* operator->() const { return object; }
    AIRefTo& operator=(T* plain) {
        object = plain;
        return *this;
    }

    T* object;
};
using AIRef = AIRefTo<CEngineObject>;
static_assert(sizeof(AIRef) == 4);
// CEngineObject::World, for the callers that name the object's base (CMapObject::World hides it).
inline CWorld* ObjectWorld(const CEngineObject* object) { return object->World(); }

// A list or vector of counted object pointers emptied: each released (head first), then cleared.
template <typename Container>
void ReleaseAndClear(Container& objects) {
    for (auto* object : objects)
        if (object) object->Release();
    objects.clear();
}
