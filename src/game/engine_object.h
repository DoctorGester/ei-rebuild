#pragma once

#include <cstdint>

class CBaseManager;

// Base of the engine's objects and managers (textures, figures, units, the managers themselves).
// Name provisional; vtable 0073B8E0, 13 slots (slots 2, 3, 11 and 12 are _purecall there). Objects
// are created by original code or by the rewritten constructor bodies, which store the original
// vtable (kVtableAddress); no C++ constructor is declared. Non-virtual members: object.cpp.
class CEngineObject {
public:
    void SetFlagBits(std::uint32_t bits);
    void ClearFlagBits(std::uint32_t bits);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073B8E0);

    virtual ~CEngineObject();                          // 0: 00402D20 (scalar deleting destructor)
    virtual void CopyFrom(const CEngineObject& other); // 1: 00402D40 (flags_, type_, prototypeId_)
    void CopyFromImpl(const CEngineObject& other);   // body of the slot above (registered by member pointer)
    virtual int Create() = 0;                          // 2
    virtual void Destroy() = 0;                        // 3
    virtual void Release();                            // 4: 00455980 (refcount; the owner releases it at 0)
    virtual void ClearState();                         // 5: 00402CC0 (empty); CUnitServer 00546080 ResetController
    virtual int RunTick();                             // 6: 00402CD0 (0); world objects' per-tick update
    virtual const char* GetName();                     // 7: 00402CE0 "noname"
    virtual const char* GetDescription();              // 8: 00402CF0 "nodesc"
    virtual void Load(void* stream);                   // 9: 00402D00 (empty); a CStream (core_streams.h)
    virtual void Save(void* stream);                   // 10: 00402D10 (empty)
    virtual const char* GetClassName() = 0;            // 11: e.g. "CTexture"
    virtual int GetClassId() = 0;                      // 12: e.g. 6 for CTexture

    // 00402D20. Slot 0's body: DestructBase, then operator delete when flags bit 0 is set.
    CEngineObject* DeletingDestruct(unsigned flags);
    // 00455940. The owning manager's context_ (the world, for the world's managers).
    void* ManagerContext() const;
    // 00455950 (app_helpers.cpp). The world the object belongs to: the manager context's slot 13
    // (the world returns itself, a party null), or null without a context.
    void* World() const;
    // 00457720 (app_helpers.cpp). Drops one reference; at zero the object is deleted (slot 0 with 1).
    void ReleaseAndDelete();
    // 00455970. Sets the owner; returns the previous one.
    CBaseManager* SetOwner(CBaseManager* owner);
    // 00455930. The destructor body: the CEngineObject vtable back, no owner.
    void DestructBase();
    // The fields the (always inlined) constructor sets, in its order: no references, id -1, no
    // flags, kind 0, no owner.
    void ClearFields();

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
