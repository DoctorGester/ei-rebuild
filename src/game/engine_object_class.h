#pragma once

// Compiled tags for engine objects built by constructor bodies (C19; docs/notes/standalone.md, "Batch 14's
// families"): a class whose objects our rewrites of game.exe's constructor bodies build (`Construct`, not a C++
// constructor) stores `Tag::Vtable()` in place of game.exe's table, and the original's dispatch through it reaches
// the tag's slots, which call the bodies game.exe's table reached (qualified: no dispatch). EngineObjectClass
// declares CEngineObject's 13 slots with the base's bodies; a class's tag derives from it (or from a family's tag
// over it) and overrides the slots its game.exe table changes. Slots 11 / 12 are ClassName / ClassId here (windows.h's
// GetClassName macro would rename them per file).

#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"

#include "lib/compiled_vtable.h"

class CStream;

extern "C" int __cdecl _purecall();

template <typename Self, typename T>
class EngineObjectClass : public ei::CompiledTag<Self> {
public:
    virtual void* DeleteScalar(unsigned flags) { return As<T>()->DeletingDestruct(flags); }   // 0
    virtual void CopyFrom(const CEngineObject& other) { Object()->CopyFromImpl(other); }       // 1: 00402D40
    virtual int Create() { return _purecall(); }                                               // 2: pure
    virtual void Destroy() { _purecall(); }                                                    // 3: pure
    virtual int Release() {   // 4: 00455980 (EAX: this, or what the manager's slot 24 leaves there)
        CEngineObject* object = Object();
        if (--object->refCount_ == 0) return object->owner_->ReleaseObject(object);
        return reinterpret_cast<int>(object);
    }
    virtual void ClearState() {}                                                               // 5: 00402CC0 (empty)
    virtual int RunTick() { return 0; }                                                        // 6: 00402CD0
    virtual const char* GetName() { return Object()->CEngineObject::GetName(); }               // 7: 00402CE0
    virtual const char* GetDescription() { return Object()->CEngineObject::GetDescription(); } // 8: 00402CF0
    virtual void Load(CStream*) {}                                                             // 9: 00402D00 (empty)
    virtual void Save(CStream*) {}                                                             // 10: 00402D10 (empty)
    virtual const char* ClassName() {                                                          // 11: pure
        _purecall();
        return nullptr;
    }
    virtual int ClassId() { return _purecall(); }   // 12: pure

protected:
    template <typename U>
    U* As() { return this->template ObjectAs<U>(); }
    CEngineObject* Object() { return this->template ObjectAs<CEngineObject>(); }
};

// game.exe's 0073B8E0 compiled (C19): CEngineObject's own table, which its constructor and destructor bodies store
// (CEngineObject::BaseVtable).
class EngineObjectBaseClass final : public EngineObjectClass<EngineObjectBaseClass, CEngineObject> {};

// CBaseManager's slots over CEngineObject's (game.exe's 0073B870): 2 / 3 / 9 / 10 the manager's, 13-24 its own (14
// _purecall), each the body game.exe's base manager table reached, called qualified.
template <typename Self, typename T>
class ManagerClass : public EngineObjectClass<Self, T> {
public:
    int Create() override { return Manager()->CBaseManager::Create(); }    // 2: 00403020
    void Destroy() override { Manager()->CBaseManager::Destroy(); }        // 3: 004031E0
    void Load(CStream* stream) override { Manager()->LoadImpl(stream); }   // 9: 004046E0
    void Save(CStream* stream) override { Manager()->SaveImpl(stream); }   // 10: 00404760
    virtual BOOL Init(CManagerContext* context) { return Manager()->InitImpl(context); }   // 13: 004031B0
    virtual CEngineObject* NewObject(int) {                                                 // 14: pure
        _purecall();
        return nullptr;
    }
    virtual void DestroyAll() { Manager()->CBaseManager::DestroyAll(); }   // 15: 004034E0
    virtual BOOL AddArchive(const char* path) { return Manager()->CBaseManager::AddArchiveImpl(path); }        // 16
    virtual BOOL RemoveArchive(const char* path) { return Manager()->CBaseManager::RemoveArchiveImpl(path); }  // 17
    virtual BOOL RemoveAllArchives() { return Manager()->CBaseManager::RemoveAllArchives(); }                  // 18
    virtual void* GetFile(const char* name, std::uint32_t* size) {                                             // 19
        return Manager()->CBaseManager::GetFileImpl(name, size);
    }
    virtual bool CloseFile(void* data) { return Manager()->CBaseManager::CloseFileImpl(data); }   // 20: 00402DA0
    virtual BOOL HasFile(const char* name) { return Manager()->CBaseManager::HasFileImpl(name); }   // 21: 00402DB0
    virtual void FreeAllFiles() {}                                                                  // 22: 00402DC0
    virtual CEngineObject* CreateObject(int type, int id) { return Manager()->CreateObjectImpl(type, id); }   // 23
    virtual int ReleaseObject(CEngineObject* object) { return Manager()->ReleaseObjectImpl(object); }        // 24

protected:
    CBaseManager* Manager() { return this->template As<CBaseManager>(); }
};

// CTransformObject's slots 13-17 over CEngineObject's (game.exe's 0073BA38 has the base's bodies there).
template <typename Self, typename T>
class TransformObjectClass : public EngineObjectClass<Self, T> {
public:
    virtual CTransformObject* CopyTransform(const CTransformObject& other) {   // 13: 00401490
        return Transform()->CopyTransformImpl(other);
    }
    virtual void CombineRotation(const Quaternion& parent) { Transform()->CombineRotationImpl(parent); }   // 14: 0040CB80
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition) {   // 15: 0040CC30
        Transform()->UpdateWorldImpl(parentRotation, parentPosition);
    }
    virtual void SaveTransform(mfc::CFileVirtuals* file) { Transform()->SaveTransformImpl(file); }   // 16: 00401510
    virtual void LoadTransform(mfc::CFileVirtuals* file) { Transform()->LoadTransformImpl(file); }   // 17: 00401540

protected:
    CTransformObject* Transform() { return this->template ObjectAs<CTransformObject>(); }
};

// game.exe's 0073BA38 compiled (C19): CTransformObject's own table (slots 2, 3, 11, 12 pure), which its
// constructor and destructor bodies store (CTransformObject::BaseVtable).
class TransformBaseClass final : public TransformObjectClass<TransformBaseClass, CTransformObject> {};
