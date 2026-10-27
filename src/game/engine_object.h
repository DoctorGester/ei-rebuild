#pragma once

#include <cstddef>
#include <cstdint>

// windows.h's GetClassName macro (GetClassNameA) renames slot 11 in a file that includes windows.h
// before this header; a derived class's override must see the same name (the headers and files that
// declare or define one undefine it too).
#undef GetClassName

class CBaseManager;
class CManagerContext;   // manager_context.h
class CStream;   // core_streams.h
class CWorld;
class CWorldClient;   // world.h
class CWorldServer;   // world.h

// Base of the engine's objects and managers (textures, figures, units, the managers themselves).
// Name provisional; game.exe's vtable 0073B8E0, 13 slots (slots 2, 3, 11 and 12 are _purecall
// there). No C++ constructor is declared: the derived classes' constructors call ClearFields, as
// game.exe inlines the base's. The default slots are defined in engine_object.cpp.
class CEngineObject {
public:
    void SetFlagBits(std::uint32_t bits);
    void ClearFlagBits(std::uint32_t bits);

    virtual ~CEngineObject();                          // 0: 00402D20 (deleting; the body 00455930: no owner)
    virtual void CopyFrom(const CEngineObject& other); // 1: 00402D40 (flags_, type_, prototypeId_)
    virtual int Create() = 0;                          // 2
    virtual void Destroy() = 0;                        // 3
    virtual void Release();                            // 4: 00455980 (refcount; the owner releases it at 0)
    virtual void ClearState();                         // 5: 00402CC0 (empty); CUnitServer 00546080 ResetController
    virtual int RunTick();                             // 6: 00402CD0 (0); world objects' per-tick update
    virtual const char* GetName();                     // 7: 00402CE0 "noname"
    virtual const char* GetDescription();              // 8: 00402CF0 "nodesc"
    virtual void Load(CStream* stream);                 // 9: 00402D00 (empty)
    virtual void Save(CStream* stream);                 // 10: 00402D10 (empty)
    virtual const char* GetClassName() = 0;            // 11: e.g. "CTexture"
    virtual int GetClassId() = 0;                      // 12: e.g. 6 for CTexture

    // The owning manager's context_ (the world, for the world's managers).
    CManagerContext* ManagerContext() const;
    // The manager context as the world, for the world's objects (units, map objects, effects, items
    // on the ground: their managers' context_ is the world itself; a party's items have the party).
    // ServerWorld / ClientWorld where the caller runs on that side. Templates so that the world's
    // classes need to be complete only where they are used.
    template <class World = CWorld> World* WorldContext() const { return static_cast<World*>(ManagerContext()); }
    template <class World = CWorldServer> World* ServerWorld() const { return WorldContext<World>(); }
    template <class World = CWorldClient> World* ClientWorld() const { return WorldContext<World>(); }
    // 00455950 (app_helpers.cpp). The world the object belongs to: the manager context's slot 13
    // (the world returns itself, a party null), or null without a context.
    CWorld* World() const;
    // 00457720 (app_helpers.cpp). Drops one reference; at zero the object is deleted (slot 0 with 1).
    void ReleaseAndDelete();
    // Sets the owner; returns the previous one.
    CBaseManager* SetOwner(CBaseManager* owner);
    // The fields the (mostly inlined) constructor sets (0040CB20 out of line), in its order: no
    // references, id -1, no flags, kind 0, no owner.
    void ClearFields();

    // Engine objects live on the game's heap (engine_object.cpp: gamecrt::operator_new / delete).
    // `new` gives null without memory and then constructs nothing, as the original's callers test.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    int refCount_;          // +0x04
    std::uint32_t flags_;   // +0x08
    int id_;                // +0x0C -1 until a manager registers the object
    int type_;              // +0x10 class-specific kind (texture usage for CTexture)
    CBaseManager* owner_;   // +0x14 manager that owns the object
    // +0x18 the database prototype: items pack its index (high word) with the material (low word;
    // PrototypeIndex / MaterialIndex, item.h), spells keep the index in the high word, units the plain
    // index. Saved and loaded with id and type by CBaseManager::Save / Load.
    std::uint32_t prototypeId_;
};
static_assert(sizeof(CEngineObject) == 0x1C);

// A counted engine object pointer (a texture, a light, a particle source, a spell) as the original's
// helpers take it: by address, thiscall on the pointer. A field that holds a T is a HeldObject<T>
// (HeldEngineObject: any engine object): reads see the object (the conversion and ->), a plain store
// (`held = p`) counts nothing. The count and the release are the engine object's.
template <typename T>
struct HeldObject {
    // 004EBE60 (RET 4; registered in refptr_copies.cpp; byte-identical to 00429B50, 00511B90,
    // 0052C690, 00556C40, 00556C60 and 0056F1A0, a particle source's): takes `value`, counted first,
    // then the old one released through its slot 4.
    void Assign(T* value) {
        CEngineObject* previous = object;
        object = value;
        if (CEngineObject* taken = value) ++taken->refCount_;
        if (previous) previous->Release();
    }
    // _Construct(p, value) of a held engine object (004FB680, 004298F0; RET 4; registered in
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
    // Release and clear.
    void Reset();
    // Hold `object` (released first unless it is already held).
    void Assign(T* newObject);
    // 0050E220 / 0050E290. Read / write the object id (-1: none); Load looks it up in `world`.
    void Load(CStream* stream, CWorld* world);
    void Save(CStream* stream);
    // 00558CA0 (AIRef only; world_deleting_destructors.cpp).
    AIRefTo* DeleteScalar(unsigned flags);
    // A counted object pointer kept in a container of plain pointers (a unit list's element) seen
    // as the reference it is.
    static AIRefTo* Of(T** slot) { return reinterpret_cast<AIRefTo*>(slot); }

    operator T*() const { return object; }
    T* operator->() const { return object; }
    // A plain store (no reference taken or released), as the original's constructors and copies.
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
