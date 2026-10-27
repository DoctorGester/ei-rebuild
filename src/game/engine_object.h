#pragma once

#include <cstddef>
#include <cstdint>

// windows.h's GetClassName macro (GetClassNameA) renames slot 11 in a file that includes windows.h
// before this header; a derived class's override must see the same name (the headers and files that
// declare or define one undefine it too).
#undef GetClassName

class CBaseManager;
class CManagerContext;
class CStream;
class CWorld;
class CWorldClient;
class CWorldServer;

// Base of the engine's objects and managers (textures, figures, units, the managers themselves).
// No C++ constructor is declared: the derived classes' constructors call ClearFields, as
// the base's is inlined elsewhere. The default slots are defined in engine_object.cpp.
class CEngineObject {
public:
    void SetFlagBits(std::uint32_t bits);
    void ClearFlagBits(std::uint32_t bits);

    virtual ~CEngineObject();                          // deleting (the body: no owner)
    virtual void CopyFrom(const CEngineObject& other); // flags_, type_, prototypeId_
    virtual int Create() = 0;
    virtual void Destroy() = 0;
    virtual void Release();                            // refcount; the owner releases it at 0
    virtual void ClearState();                         // empty; CUnitServer ResetController
    virtual int RunTick();                             // 0; world objects' per-tick update
    virtual const char* GetName();                     // "noname"
    virtual const char* GetDescription();              // "nodesc"
    virtual void Load(CStream* stream);                 // empty
    virtual void Save(CStream* stream);                 // empty
    virtual const char* GetClassName() = 0;            // e.g. "CTexture"
    virtual int GetClassId() = 0;                      // e.g. 6 for CTexture

    CManagerContext* ManagerContext() const;
    // The manager context as the world, for the world's objects (units, map objects, effects, items
    // on the ground: their managers' context_ is the world itself; a party's items have the party).
    // ServerWorld / ClientWorld where the caller runs on that side. Templates so that the world's
    // classes need to be complete only where they are used.
    template <class World = CWorld> World* WorldContext() const { return static_cast<World*>(ManagerContext()); }
    template <class World = CWorldServer> World* ServerWorld() const { return WorldContext<World>(); }
    template <class World = CWorldClient> World* ClientWorld() const { return WorldContext<World>(); }
    // The world the object belongs to: the manager context's slot 13
    // (the world returns itself, a party null), or null without a context.
    CWorld* World() const;
    void ReleaseAndDelete();
    CBaseManager* SetOwner(CBaseManager* owner);
    // The fields the (mostly inlined) constructor sets, in its order: no
    // references, id -1, no flags, kind 0, no owner.
    void ClearFields();

    // Engine objects live on the game's heap (engine_object.cpp: gamecrt::operator_new / delete).
    // `new` gives null without memory and then constructs nothing, as the callers test.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    int refCount_;
    std::uint32_t flags_;
    int id_;                // -1 until a manager registers the object
    int type_;              // class-specific kind (texture usage for CTexture)
    CBaseManager* owner_;   // manager that owns the object
    // the database prototype: items pack its index (high word) with the material (low word;
    // PrototypeIndex / MaterialIndex, item.h), spells keep the index in the high word, units the plain
    // index. Saved and loaded with id and type by CBaseManager::Save / Load.
    std::uint32_t prototypeId_;
};
static_assert(sizeof(CEngineObject) == 0x1C);

// A counted engine object pointer (a texture, a light, a particle source, a spell) as the game's
// helpers take it: by address, thiscall on the pointer. A field that holds a T is a HeldObject<T>
// (HeldEngineObject: any engine object): reads see the object (the conversion and ->), a plain store
// (`held = p`) counts nothing. The count and the release are the engine object's.
template <typename T>
struct HeldObject {
    // Registered in refptr_copies.cpp, with byte-identical copies (a particle source's among them):
    // takes `value`, counted first,
    // then the old one released through its slot 4.
    void Assign(T* value) {
        CEngineObject* previous = object;
        object = value;
        if (CEngineObject* taken = value) ++taken->refCount_;
        if (previous) previous->Release();
    }
    // _Construct(p, value) of a held engine object (registered in
    // library_containers.cpp): the pointer copied, its count raised when not null; returns this.
    HeldObject* Construct(T* const* value) {
        object = *value;
        if (CEngineObject* taken = object) ++taken->refCount_;
        return this;
    }
    // A counted pointer kept in a container of plain pointers (a list's or a vector's element)
    // seen as the holder it is.
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

// A counted reference (CEngineObject refcount at +4, released through vtable slot 4). The AI's
// reference fields (orders, modes, the brain's target, the reports) and the effects' figures are
// AIRefTo<T> of the class they hold (AIRef: any engine object): reads see the object (the
// conversion and ->); Assign / Reset count, a plain store (`ref = p`) does not. The bodies are
// counted_ref.cpp's, instantiated there for each T; AIRef's are the registered ones.
template <typename T>
struct AIRefTo {
    void Reset();
    void Assign(T* newObject);
    // Read / write the object id (-1: none); Load looks it up in `world`.
    void Load(CStream* stream, CWorld* world);
    void Save(CStream* stream);
    AIRefTo* DeleteScalar(unsigned flags);
    // A counted object pointer kept in a container of plain pointers (a unit list's element) seen
    // as the reference it is.
    static AIRefTo* Of(T** slot) { return reinterpret_cast<AIRefTo*>(slot); }

    operator T*() const { return object; }
    T* operator->() const { return object; }
    // A plain store (no reference taken or released), as the constructors and copies do.
    AIRefTo& operator=(T* plain) {
        object = plain;
        return *this;
    }

    T* object;
};
using AIRef = AIRefTo<CEngineObject>;
template <>
AIRef* AIRef::DeleteScalar(unsigned flags);
static_assert(sizeof(AIRef) == 4);
// CEngineObject::World, for the callers that name the object's base (CMapObject::World
// hides it).
inline CWorld* ObjectWorld(const CEngineObject* object) { return object->World(); }
