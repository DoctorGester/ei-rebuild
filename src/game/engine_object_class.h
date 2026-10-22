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
    virtual void SaveTransform(void* file) { Transform()->SaveTransformImpl(file); }   // 16: 00401510
    virtual void LoadTransform(void* file) { Transform()->LoadTransformImpl(file); }   // 17: 00401540

protected:
    CTransformObject* Transform() { return this->template ObjectAs<CTransformObject>(); }
};
