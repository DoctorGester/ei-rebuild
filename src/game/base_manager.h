#pragma once

#include "engine_object.h"

#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

using BOOL = int;

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
    int Create() override;
    void Destroy() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // Keeps the context and creates; a failed create clears the context again. Returns 1 / 0.
    virtual BOOL Init(CManagerContext* context);
    // A new object of `type` (null when the type is unknown).
    virtual CEngineObject* NewObject(int type) = 0;
    // Destroys and releases every object, then empties the registration tables.
    virtual void DestroyAll();
    // The archive interface of the managers that load from resource archives
    // (CResourceManager overrides them); here they do nothing and return 0 / null / false.
    virtual BOOL AddArchive(const std::string& path);
    virtual BOOL RemoveArchive(const std::string& path);
    virtual BOOL RemoveAllArchives();
    virtual void* GetFile(std::string_view name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual BOOL HasFile(std::string_view name);
    virtual void FreeAllFiles();
    // A spare object of `type`, or a new one (NewObject), registered under `id` or, for -1, under a free id;
    // the manager becomes its owner and it is created. Null when NewObject fails.
    virtual CEngineObject* CreateObject(int type, int id);
    template <class T> T* CreateObjectAs(int type, int id) { return static_cast<T*>(CreateObject(type, id)); }
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
    ObjectMap objects_;             // registered objects by id
    int maxId_;                     // highest id registered
    std::list<CEngineObject*> objectList_;         // registered objects in registration order (saved so)
    std::unordered_map<int, std::list<CEngineObject*>::iterator> positions_;         // id -> its node in objectList_
    int maxPositionId_;             // highest key of positions_ (kept by RegisterInstant)
    SpareMap spares_;               // type -> list of spare objects (allocated per type)
    int maxSpareType_;              // highest key of spares_
    std::unordered_map<int, int> spareCounts_;     // type -> number of spares
    int maxSpareCountType_;         // highest key of spareCounts_
    int idLow_;                     // automatic ids come from [idLow_, idHigh_)
    int idHigh_;                    // 2000000000
    int reservedA_;                 // -1 when constructed, 0 after Create; nothing reads it
    int reservedB_;                 // -1 when constructed, 300 after Create; nothing reads it
    int lastId_;                    // id most recently registered (-1 when constructed)
    CManagerContext* context_;      // set by Init / SetContext (the world for its managers, a party for its own)
    // context_ as the world, for the world's managers (a party's item manager has the party).
    template <class World = CWorld> World* ContextAsWorld() const { return static_cast<World*>(context_); }
};

// Walks a manager's objectList_. While iterating it holds a reference on the manager (refCount_ + 1); the
// owner drops it afterwards with `--manager->refCount_` (the destructor).
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

class CClientSpecificManager : public CBaseManager {
public:
    CClientSpecificManager();
    ~CClientSpecificManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // A new object of `type` ('E' .. 'K'), or null for 'J', any other type or a failed allocation.
    CEngineObject* NewObject(int type) override;
};
class CEffectManager : public CBaseManager {
public:
    CEffectManager();
    ~CEffectManager() override;
    // CBaseManager's Create, as 0 / 1.
    int Create() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    CEngineObject* NewObject(int type) override;
};
