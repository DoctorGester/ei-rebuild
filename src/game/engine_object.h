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

// CEngineObject::GetClassId: the class of an engine object or manager, named kClass + the class's name here
// (GetClassName gives the original's). The effect manager creates its effects by it too. Two classes share
// 0x0E and two 0x21.
enum ClassId : int {
    kClassMaterial = 0x05,
    kClassTexture = 0x06,
    kClassWorldClient = 0x07,
    kClassWorldServer = 0x08,
    kClassParty = 0x09,
    kClassLight = 0x0A,
    kClassTerrain = 0x0B,
    kClassParticleSource = 0x0C,
    kClassSysInfo = 0x0D,
    kClassSoundManager = 0x0E,
    kClassMusicSystem = 0x0E,
    kClassPlayer = 0x0F,
    kClassItemWeapon = 0x11,
    kClassItemArmor = 0x12,
    kClassItemQuick = 0x13,
    kClassItemQuest = 0x14,
    kClassItemLoot = 0x15,
    kClassItemSpell = 0x16,
    kClassSpellContainer = 0x1A,
    kClassSpellConstructor = 0x1B,
    kClassItemConstructor = 0x1C,
    kClassRainSnow = 0x1D,
    kClassLightning = 0x1E,
    kClassHoover = 0x1F,
    kClassSkyObject = 0x20,
    kClassFigureBone = 0x21,
    kClassFigureAnimationState = 0x21,
    kClassAnimationTrack = 0x22,
    kClassTextureManager = 0x26,
    kClassMaterialManager = 0x27,
    kClassFigureManager = 0x28,
    kClassLightFactory = 0x29,
    kClassTextManager = 0x2A,
    kClassParticleManager = 0x2C,
    kClassMapObjectManager = 0x2D,
    kClassPlayerManager = 0x2E,
    kClassItemManager = 0x2F,
    kClassClientSpecificManager = 0x30,
    kClassServerSpecificManager = 0x31,
    kClassEffectManager = 0x32,
    kClassSpellManager = 0x33,
    kClassPlatformSet = 0x34,
    kClassLightningManager = 0x35,
    kClassFigureMeshDrawer = 0x36,
    kClassObject3DClientSpecific = 0x37,
    kClassObject3DServer = 0x38,
    kClassMapObjectServer = 0x39,
    kClassMapObjectClientSpecific = 0x3A,
    kClassPlayerServer = 0x3B,
    kClassPlayerClient = 0x3C,
    kClassUnitClientSpecific = 0x3D,
    kClassUnitServer = 0x3E,
    kClassTorchObjectClientSpec = 0x41,
    kClassTorchObjectSpec = 0x42,
    kClassCamera = 0x43,
    kClassFigureMesh = 0x44,
    kClassUiWindow = 0x45,
    kClassUiMesh = 0x46,
    kClassEffectSound = 0x48,
    kClassEffectAmbientSound = 0x49,
    kClassEffectParticle = 0x4A,
    kClassEffectPointLight = 0x4B,
    kClassEffectLightning = 0x4D,
    kClassEffectArrow = 0x4E,
    kClassSpell = 0x4F,
    kClassUnit = 0x50,
    kClassTree = 0x52,
    kClassStone = 0x53,
    kClassBuilding = 0x54,
    kClassTrapObject = 0x55,
    kClassStuffObject = 0x56,
    kClassTorchObject = 0x57,
    kClassEffectWall = 0x5A,
    kClassEffectArrowFigure = 0x5B,
    kClassLever = 0x5D,
    kClassEffectShowVisibility = 0x5E,
    kClassVisibilityFog = 0x5F,
    kClassClientArrayManager = 0x60,
    kClassMagicTrap = 0x61,
    kClassEffectSoundSeq = 0x63,
    kClassEffectMoshka = 0x64,
    kClassEffectTornado = 0x65,
};

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
    virtual ClassId GetClassId() = 0;

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
// Kept: names the one instance of the AI reference template that the units use.
using AIRef = AIRefTo<CEngineObject>;
// CEngineObject::World, for the callers that name the object's base (CMapObject::World hides it).
inline CWorld* ObjectWorld(const CEngineObject* object) { return object->World(); }

// A list or vector of counted object pointers emptied: each released (head first), then cleared.
template <typename Container>
void ReleaseAndClear(Container& objects) {
    for (auto* object : objects)
        if (object) object->Release();
    objects.clear();
}
