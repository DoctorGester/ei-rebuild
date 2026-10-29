#pragma once

#include "engine_object.h"

#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>


struct FigureAnimation;
class CManagerContext;

// Base of every engine manager (textures, figures, objects, lightning, ...): creates objects by type, registers
// them by id, saves and loads the registered ones, and keeps the objects released to it as spares of their type
// for reuse.
class CBaseManager : public CEngineObject {
public:
    // The animation `name` (case-insensitive) in the database "<figure>.adb" of this manager (the world's map
    // object manager), or null; a missing database is reported to the debug log.
    FigureAnimation* FindFigureAnimation(const std::string& figure, const char* name);

    // The objects and the spares are walked in STLport's order; the positions and spare counts are only looked up.
    // Kept order: Destroy / DestroyAll destroy and release the objects in it (each Destroy changes game state).
    using ObjectMap = lib::HashMap<unsigned, CEngineObject*>;
    // Kept order: FreeSpares deletes the spares in it, and their destructors release counted members (a particle
    // source's texture) into other managers' spare lists, which later creations reuse in order.
    using SpareMap = lib::HashMap<int, std::list<CEngineObject*>*>;

    // Empties the tables and sizes them for a new session; returns 1.
    void Create() override;
    void Destroy() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // Keeps the context and creates.
    virtual void Init(CManagerContext* context);
    // A new object of `type` (null when the type is unknown).
    virtual CEngineObject* NewObject(int type) = 0;
    // Destroys and releases every object, then empties the registration tables.
    virtual void DestroyAll();
    // The archive interface of the managers that load from resource archives
    // (CResourceManager overrides them); here they do nothing and return 0 / null / false.
    virtual bool AddArchive(const std::string& path);
    virtual bool RemoveArchive(const std::string& path);
    virtual bool RemoveAllArchives();
    virtual void* GetFile(std::string_view name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual bool HasFile(std::string_view name);
    virtual void FreeAllFiles();
    // A spare object of `type`, or a new one (NewObject), registered under `id` or, for -1, under a free id;
    // the manager becomes its owner and it is created. Null when NewObject fails.
    virtual CEngineObject* CreateObject(int type, int id);
    template <class T> T* CreateObjectAs(int type, int id) { return static_cast<T*>(CreateObject(type, id)); }
    // The same for a manager whose types are an enum class (MapObjectType, SideObjectType): converted once here.
    template <class T, class Type>
        requires std::is_enum_v<Type>
    T* CreateObjectAs(Type type, int id) { return static_cast<T*>(CreateObject(static_cast<int>(type), id)); }
    // An object without references leaves the manager: unregistered, destroyed, without owner, then kept as a
    // spare. Returns RemoveObject's 0 (nothing meaningful when nothing is done).
    virtual int ReleaseObject(CEngineObject* object);

    // Runs CBaseManager's Destroy.
    ~CBaseManager() override;
    CEngineObject* FindObject(unsigned id) const;
    template <class T> T* FindObjectAs(unsigned id) const { return static_cast<T*>(FindObject(id)); }
    void SetContext(CManagerContext* context);
    // The object registered under `id` leaves the registration tables (it stays alive
    // and keeps its id); returns it, or null.
    CEngineObject* Unregister(int id);
    // Registers an object made elsewhere under a free id and becomes its owner; returns the id.
    int RegisterInstant(CEngineObject* object);
    // Unregisters the object (when registered under its id) and keeps it as a spare of
    // its type with id -1. Returns 0.
    int RemoveObject(CEngineObject* object);
    void FreeSpares();
    // The registered objects' own data (Load / Save) by id, up to an end mark.
    void LoadObjects(CStream* stream);
    void SaveObjects(CStream* stream);
    // Every object without references gets one and releases it, so the ones nobody took go back to the spares.
    void ReleaseUnused();

protected:
    // Empty tables (the objects and spares sized 100), no context, ids up to 2000000000.
    CBaseManager();

public:
    ObjectMap objects;              // registered objects by id
    int maxId;                      // highest id registered
    std::list<CEngineObject*> objectList;          // registered objects in registration order (saved so)
    std::unordered_map<int, std::list<CEngineObject*>::iterator> positions;          // id -> its node in objectList
    int maxPositionId;              // highest key of positions (kept by RegisterInstant)
    SpareMap spares;                // type -> list of spare objects (allocated per type)
    int maxSpareType;               // highest key of spares
    std::unordered_map<int, int> spareCounts;      // type -> number of spares
    int maxSpareCountType;          // highest key of spareCounts
    int idLow;                      // automatic ids come from [idLow, idHigh)
    int idHigh;                     // 2000000000
    int reservedA;                  // -1 when constructed, 0 after Create; nothing reads it
    int reservedB;                  // -1 when constructed, 300 after Create; nothing reads it
    int lastId;                     // id most recently registered (-1 when constructed)
    CManagerContext* context;       // set by Init / SetContext (the world for its managers, a party for its own)
    // context as the world, for the world's managers (a party's item manager has the party).
    template <class World = CWorld> World* ContextAsWorld() const { return static_cast<World*>(context); }
};

// Walks a manager's objectList. While iterating it holds a reference on the manager (refCount + 1); the
// owner drops it afterwards with `manager->ForgetReference()` (the destructor).
struct ObjectListIterator {
    CBaseManager* manager;
    std::list<CEngineObject*>::iterator position;   // the node Next returns (the list's end: none)

    CEngineObject* First(CBaseManager* list);
    CEngineObject* Next();
};

// The same walk over a manager whose objects are all of class T (the light factory's lights, an
// item manager's items): First and Next hand them out typed. The user drops the manager's count.
template <typename T>
struct ObjectListIteratorOf : ObjectListIterator {
    T* First(CBaseManager* list) { return static_cast<T*>(ObjectListIterator::First(list)); }
    T* Next() { return static_cast<T*>(ObjectListIterator::Next()); }
};

// The types of the side-specific manager's objects (CEngineObject::type; the client's factory spells them 'E'..'K').
// The server's manager makes no Drawer (0x45); nothing has 0x4A.
enum class SideObjectType : int {
    Drawer = 0x45,       // FigureMeshDrawer
    Object3D = 0x46,     // CObject3DClientSpecific / CObject3DServer
    MapObject = 0x47,    // CMapObjectClientSpecific / CMapObjectServer
    Unit = 0x48,         // CUnitClientSpecific / CUnitServer
    Player = 0x49,       // CPlayerClient / CPlayerServer
    Torch = 0x4B,        // CTorchObjectClientSpec / CTorchObjectSpec
};

// type of a side-specific half.
inline SideObjectType SideTypeOf(const CEngineObject* object) { return static_cast<SideObjectType>(object->type); }

class CClientSpecificManager : public CBaseManager {
public:
    CClientSpecificManager();
    ~CClientSpecificManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // A new object of a SideObjectType `type`, or null for any other type or a failed allocation.
    CEngineObject* NewObject(int type) override;
};
class CEffectManager : public CBaseManager {
public:
    CEffectManager();
    ~CEffectManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    CEngineObject* NewObject(int type) override;
};
