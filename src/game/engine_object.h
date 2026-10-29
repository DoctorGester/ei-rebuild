#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "ref.h"

// `new (kZeroed) T(...)`: T constructed on a zeroed block, for the engine objects whose constructors leave fields
// unset (the two factories' server and map objects, the lights, the particle sources): a determinism fix the
// standalone keeps; the harness zeroes the original's at the same calls (tests/verify/harness_leftovers.h).
// `delete` frees them as any other.
struct ZeroFill {};
inline constexpr ZeroFill kZeroed{};
inline void* operator new(std::size_t size, ZeroFill) { return std::memset(::operator new(size), 0, size); }
inline void operator delete(void* block, ZeroFill) noexcept { ::operator delete(block); }

// EngineObject::id of an object no manager registered; the saves' and messages' "no object".
inline constexpr int kNoObjectId = -1;

// EngineObject::GetClassId: the class of an engine object or manager, named kClass + the class's name here
// (ClassName gives the original's). The effect manager creates its effects by it too. Two classes share
// 0x0E and two 0x21.
enum class ClassId : int {
    Material = 0x05,
    Texture = 0x06,
    WorldClient = 0x07,
    WorldServer = 0x08,
    Party = 0x09,
    Light = 0x0A,
    Terrain = 0x0B,
    ParticleSource = 0x0C,
    SysInfo = 0x0D,
    SoundManager = 0x0E,
    MusicSystem = 0x0E,
    Player = 0x0F,
    ItemWeapon = 0x11,
    ItemArmor = 0x12,
    ItemQuick = 0x13,
    ItemQuest = 0x14,
    ItemLoot = 0x15,
    ItemScroll = 0x16,
    SpellContainer = 0x1A,
    SpellConstructor = 0x1B,
    ItemConstructor = 0x1C,
    RainSnow = 0x1D,
    Lightning = 0x1E,
    Hoover = 0x1F,
    SkyObject = 0x20,
    FigureBone = 0x21,
    FigureAnimationState = 0x21,
    AnimationTrack = 0x22,
    TextureManager = 0x26,
    MaterialManager = 0x27,
    FigureManager = 0x28,
    LightFactory = 0x29,
    TextManager = 0x2A,
    ParticleManager = 0x2C,
    MapObjectManager = 0x2D,
    PlayerManager = 0x2E,
    ItemManager = 0x2F,
    ClientSpecificManager = 0x30,
    ServerSpecificManager = 0x31,
    EffectManager = 0x32,
    SpellManager = 0x33,
    PlatformSet = 0x34,
    LightningManager = 0x35,
    FigureMeshDrawer = 0x36,
    Object3DClientSpecific = 0x37,
    Object3DServer = 0x38,
    MapObjectServer = 0x39,
    MapObjectClientSpecific = 0x3A,
    PlayerServer = 0x3B,
    PlayerClient = 0x3C,
    UnitClientSpecific = 0x3D,
    UnitServer = 0x3E,
    TorchObjectClientSpec = 0x41,
    TorchObjectSpec = 0x42,
    Camera = 0x43,
    FigureMesh = 0x44,
    UiWindow = 0x45,
    UiMesh = 0x46,
    EffectSound = 0x48,
    EffectAmbientSound = 0x49,
    EffectParticle = 0x4A,
    EffectPointLight = 0x4B,
    EffectLightning = 0x4D,
    EffectArrow = 0x4E,
    Spell = 0x4F,
    Unit = 0x50,
    Tree = 0x52,
    Stone = 0x53,
    Building = 0x54,
    TrapObject = 0x55,
    StuffObject = 0x56,
    TorchObject = 0x57,
    EffectWall = 0x5A,
    EffectArrowFigure = 0x5B,
    Lever = 0x5D,
    EffectShowVisibility = 0x5E,
    VisibilityFog = 0x5F,
    ClientArrayManager = 0x60,
    MagicTrap = 0x61,
    EffectSoundSeq = 0x63,
    EffectMoshka = 0x64,
    EffectTornado = 0x65,
};

class BaseManager;
class CManagerContext;
class ByteStream;
class CWorld;
class WorldClient;
class WorldServer;

// Base of the engine's objects and managers. A spare a manager hands out again (BaseManager::CreateObject) is not
// constructed again: the manager sets its flags, type, id and owner.
class EngineObject {
public:
    EngineObject() = default;
    virtual ~EngineObject();
    virtual void CopyFrom(const EngineObject& other); // flags, type, prototypeId
    virtual void Create() = 0;
    virtual void Destroy() = 0;
    virtual void Release();                            // refcount; the owner releases it at 0
    virtual void ClearState();
    virtual bool RunTick();
    virtual const char* GetName();                     // "noname"
    virtual const char* GetDescription();              // "nodesc"
    virtual void Load(ByteStream* stream);
    virtual void Save(ByteStream* stream);
    virtual const char* ClassName() = 0;
    virtual ClassId GetClassId() = 0;

    void AddRef() { ++refCount; }
    // Drops a reference without the release at zero, where the original did (sic): the object stays.
    void ForgetReference() { --refCount; }

    CManagerContext* ManagerContext() const;
    // The manager context as the world, for the world's objects (a party's items have the party instead).
    // Templates so that the world's classes need to be complete only where they are used.
    template <class World = CWorld> World* WorldContext() const { return static_cast<World*>(ManagerContext()); }
    template <class World = WorldServer> World* ServerWorld() const { return WorldContext<World>(); }
    template <class World = WorldClient> World* ClientWorld() const { return WorldContext<World>(); }
    // The world the object belongs to, or null without a context.
    CWorld* World() const;
    BaseManager* SetOwner(BaseManager* owner);

    int refCount = 0;
    std::uint32_t flags = 0;
    int id = kNoObjectId;   // until a manager registers the object
    int type = 0;           // class-specific kind (texture usage for CTexture)
    BaseManager* owner = nullptr;
    // The database prototype: items pack its index (high word) with the material (low word), spells keep the
    // index in the high word, units the plain index.
    std::uint32_t prototypeId = 0;
};

// A counted engine object's id saved (-1: none) or read back: -1 resets `ref`, any other id is looked up in
// `world`'s map object manager.
void SaveRef(const EngineObject* object, ByteStream* stream);
template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, CWorld* world);

// A list or vector of counted object pointers emptied: each released (head first), then cleared.
template <typename Container>
void ReleaseAndClear(Container& objects) {
    for (auto* object : objects)
        if (object) object->Release();
    objects.clear();
}
