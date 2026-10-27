#pragma once

#include "engine_object.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include <string>

#undef GetClassName   // CEngineObject::GetClassName

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

    using ObjectMap = stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>;
    using ObjectList = mfc::CTypedPtrList<CEngineObject>;
    using PositionMap = stlp::hash_map<int, ObjectList::Node*, stlp::IntHash>;
    using SpareMap = stlp::hash_map<int, ObjectList*, stlp::IntHash>;
    using SpareCountMap = stlp::hash_map<int, int, stlp::IntHash>;

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
    virtual BOOL AddArchive(const char* path);
    virtual BOOL RemoveArchive(const char* path);
    virtual BOOL RemoveAllArchives();
    virtual void* GetFile(const char* name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual BOOL HasFile(const char* name);
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
    ObjectList objectList_;         // registered objects in registration order (saved so)
    PositionMap positions_;         // id -> its node in objectList_
    int maxPositionId_;             // highest key of positions_ (kept by RegisterInstant)
    SpareMap spares_;               // type -> list of spare objects (allocated per type)
    int maxSpareType_;              // highest key of spares_
    SpareCountMap spareCounts_;     // type -> number of spares
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
static_assert(offsetof(CBaseManager, objects_) == 0x1C);
static_assert(offsetof(CBaseManager, maxId_) == 0x30);
static_assert(offsetof(CBaseManager, objectList_) == 0x34);
static_assert(offsetof(CBaseManager, positions_) == 0x50);
static_assert(offsetof(CBaseManager, maxPositionId_) == 0x64);
static_assert(offsetof(CBaseManager, spares_) == 0x68);
static_assert(offsetof(CBaseManager, spareCounts_) == 0x80);
static_assert(offsetof(CBaseManager, idLow_) == 0x98);
static_assert(offsetof(CBaseManager, lastId_) == 0xA8);
static_assert(offsetof(CBaseManager, context_) == 0xAC);
static_assert(sizeof(CBaseManager) == 0xB0);

// Walks a manager's objectList_. While iterating it holds a reference on the manager (refCount_ + 1); the
// owner drops it afterwards with `--manager->refCount_` (the inlined destructor).
struct ObjectListIterator {
    CBaseManager* manager;
    CBaseManager::ObjectList::Node* position;   // the node Next returns

    CEngineObject* First(CBaseManager* list);
    CEngineObject* Next();
};
static_assert(sizeof(ObjectListIterator) == 8);

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
    const char* GetClassName() override;
    int GetClassId() override;               // 0x30
    // A new object of `type` ('E' .. 'K'), or null for 'J', any other type or a failed allocation.
    CEngineObject* NewObject(int type) override;

};
namespace misc400 {
void __fastcall ManagerDestruct(CBaseManager* self);
} // namespace misc400

// T's constructor on `memory`, as the map object factory takes it.
template <typename T>
void* __fastcall ConstructOn(void* memory) {
    return new (memory) T();
}

class CEffectManager : public CBaseManager {
public:
    CEffectManager();
    ~CEffectManager() override;
    // CBaseManager's Create, as 0 / 1.
    int Create() override;
    const char* GetClassName() override;
    int GetClassId() override;               // 0x32
    CEngineObject* NewObject(int type) override;
};
