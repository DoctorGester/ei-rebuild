#pragma once

#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

using BOOL = int;

struct FigureAnimation;   // map_object_figures.h

// Base of every engine manager (textures, figures, objects, lightning, ...): creates objects by
// type, registers them by id, saves and loads the registered ones, and keeps the objects released
// to it as spares of their type for reuse. Name from the game's own error strings
// ("CBaseManager::RegisterInstant"). vtable 0073B870, 25 slots (0-12 as CEngineObject's; 11, 12
// and 14 are _purecall there). base_manager.cpp, base_manager_core.cpp.
class CBaseManager : public CEngineObject {
public:
    // 00512F60 (combat_order_setters.cpp, RET 8). The animation `name` (case-insensitive) in the
    // database "<figure>.adb" of this manager (the world's map object manager), or null; a missing
    // database is reported to the debug log.
    FigureAnimation* FindFigureAnimation(const mfc::CString& figure, const char* name);
    static constexpr std::uintptr_t kVtableAddress = 0x0073B870;

    using ObjectMap = stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>;
    using ObjectList = mfc::CTypedPtrList<CEngineObject>;
    using PositionMap = stlp::hash_map<int, ObjectList::Node*, stlp::IntHash>;
    using SpareMap = stlp::hash_map<int, ObjectList*, stlp::IntHash>;
    using SpareCountMap = stlp::hash_map<int, int, stlp::IntHash>;

    // Slot 0 is the scalar deleting destructor 00402DD0 (DeletingDestruct).
    // 2: 00403020. Empties the tables and sizes them for a new session; returns 1.
    int Create() override;
    // 3: 004031E0. Destroys, releases and deletes every object and clears the tables.
    void Destroy() override;
    // 9: 004046E0. Frees the spares and creates the objects listed in the stream.
    void Load(void* stream) override;
    void LoadImpl(void* stream);   // body of the slot above (registered by member pointer)
    // 10: 00404760. Writes {id, type, prototypeId_} of every registered object.
    void Save(void* stream) override;
    void SaveImpl(void* stream);   // body of the slot above (registered by member pointer)

    // 13: 004031B0. Keeps the context and creates (slot 2); a failed create clears the context
    // again. Returns 1 / 0.
    virtual BOOL Init(void* context);
    BOOL InitImpl(void* context);   // body of the slot above (registered by member pointer)
    // 14: a new object of `type` (null when the type is unknown).
    virtual CEngineObject* NewObject(int type) = 0;
    // 15: 004034E0. Destroys and releases every object, then empties the registration tables.
    virtual void DestroyAll();
    // 16-22: the archive interface of the managers that load from resource archives
    // (CResourceManager overrides them); here they do nothing and return 0 / null / false.
    virtual BOOL AddArchive(const char* path);                     // 16: 00402D60
    BOOL AddArchiveImpl(const char* path);   // body of the slot above (registered by member pointer)
    virtual BOOL RemoveArchive(const char* path);                  // 17: 00402D70
    BOOL RemoveArchiveImpl(const char* path);   // body of the slot above (registered by member pointer)
    virtual BOOL RemoveAllArchives();                              // 18: 00402D80
    virtual void* GetFile(const char* name, std::uint32_t* size);  // 19: 00402D90
    void* GetFileImpl(const char* name, std::uint32_t* size);   // body of the slot above (registered by member pointer)
    virtual bool CloseFile(void* data);                            // 20: 00402DA0
    bool CloseFileImpl(void* data);   // body of the slot above (registered by member pointer)
    virtual BOOL HasFile(const char* name);                        // 21: 00402DB0
    BOOL HasFileImpl(const char* name);   // body of the slot above (registered by member pointer)
    virtual void FreeAllFiles();                                   // 22: 00402DC0
    // 23: 00403730. A spare object of `type`, or a new one (slot 14), registered under `id` or,
    // for -1, under a free id; the manager becomes its owner and it is created (slot 2). Null
    // when slot 14 fails.
    virtual CEngineObject* CreateObject(int type, int id);
    CEngineObject* CreateObjectImpl(int type, int id);   // body of the slot above (registered by member pointer)
    // 24: 00403AE0. An object without references leaves the manager: unregistered, destroyed
    // (slot 3), without owner, then kept as a spare. Returns RemoveObject's 0 (the original's EAX
    // is not meaningful when nothing is done).
    virtual int ReleaseObject(CEngineObject* object);
    int ReleaseObjectImpl(CEngineObject* object);   // body of the slot above (registered by member pointer)

    // 00402B20. The constructor body; returns this.
    CBaseManager* ConstructBase();
    // 00402DF0. The destructor body: Destroy (slot 3), the containers, the engine object.
    void DestructBody();
    // 00402DD0. Slot 0's body: DestructBody, then operator delete when flags bit 0 is set.
    CBaseManager* DeletingDestruct(unsigned flags);
    // 00403F40. Object registered under `id`, or null.
    CEngineObject* FindObject(unsigned id) const;
    // 00403720. Sets context_ (as Init does).
    void SetContext(void* context);
    // 004039B0. The object registered under `id` leaves the registration tables (it stays alive
    // and keeps its id); returns it, or null.
    CEngineObject* Unregister(int id);
    // 00403BD0. Registers an object made elsewhere under a free id and becomes its owner; returns
    // the id.
    int RegisterInstant(CEngineObject* object);
    // 00404040. Unregisters the object (when registered under its id) and keeps it as a spare of
    // its type with id -1. Returns 0.
    int RemoveObject(CEngineObject* object);
    // 00404410. Deletes the spare objects and their lists and empties the spare tables.
    void FreeSpares();
    // 004047E0 / 00404860. The registered objects' own data (slots 9 / 10) by id, up to an end
    // mark.
    void LoadObjects(void* stream);
    void SaveObjects(void* stream);
    // 004048F0. Every object without references gets one and releases it (slot 4), so the ones
    // nobody took go back to the spares.
    void ReleaseUnused();

    ObjectMap objects_;             // +0x1C registered objects by id
    int maxId_;                     // +0x30 highest id registered
    ObjectList objectList_;         // +0x34 registered objects in registration order (saved so)
    PositionMap positions_;         // +0x50 id -> its node in objectList_
    int maxPositionId_;             // +0x64 highest key of positions_ (kept by RegisterInstant)
    SpareMap spares_;               // +0x68 type -> list of spare objects (allocated per type)
    int maxSpareType_;              // +0x7C highest key of spares_
    SpareCountMap spareCounts_;     // +0x80 type -> number of spares
    int maxSpareCountType_;         // +0x94 highest key of spareCounts_
    int idLow_;                     // +0x98 automatic ids come from [idLow_, idHigh_)
    int idHigh_;                    // +0x9C 2000000000
    int reservedA_;                 // +0xA0 -1 when constructed, 0 after Create; nothing reads it
    int reservedB_;                 // +0xA4 -1 when constructed, 300 after Create; nothing reads it
    int lastId_;                    // +0xA8 id most recently registered (-1 when constructed)
    void* context_;                 // +0xAC set by Init / SetContext (the world for its managers)
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

// Walks a manager's objectList_. While iterating it holds a reference on the manager (refCount_ +
// 1); the owner drops it afterwards with `--manager->refCount_` (the original's inlined
// destructor). base_manager.cpp.
struct ObjectListIterator {
    CBaseManager* manager;
    CBaseManager::ObjectList::Node* position;   // the node Next returns

    CEngineObject* First(CBaseManager* list);   // 00404920
    CEngineObject* Next();                      // 00404960
};
static_assert(sizeof(ObjectListIterator) == 8);

// The client-specific manager (vtable 0073BECC, class id 0x30; misc_400.cpp): only its object factory
// has a body in a class here, the other slots are rewritten as free functions.
class CClientSpecificManager : public CBaseManager {
public:
    // Body of the slot at 0042D700 (registered by member pointer).
    CEngineObject* NewObjectImpl(int type);
};

// The effect manager (class of the effects; world_effect_lifetime.cpp / world_managers.cpp).
class CEffectManager : public CBaseManager {
public:
    // Body of the slot at 0055F380 (registered by member pointer).
    CBaseManager* DeleteScalar(unsigned flags);
    // Body of the slot at 0067E170 (registered by member pointer).
    CEngineObject* NewObjectImpl(int type);
};
