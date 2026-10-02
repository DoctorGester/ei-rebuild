#pragma once

#include <cstdint>

// Base of the engine's objects and managers (textures, figures, the managers themselves). Name
// provisional. The virtual interface below is the part shared by every vtable seen so far; slot
// numbers are in comments. Objects are created by original code, so no constructor is declared.
class CEngineObject {
public:
    virtual ~CEngineObject();                          // 0
    virtual void CopyFrom(const CEngineObject& other); // 1: 00402D40 (copies +0x08, +0x10, +0x18)
    virtual int Create();                              // 2
    virtual void Destroy();                            // 3
    virtual void Release();                            // 4: 00455980 (refcount; owner frees at 0)
    virtual void Slot5();                              // 5
    virtual int Slot6();                               // 6
    virtual const char* GetName();                     // 7: "noname" by default
    virtual const char* GetDescription();              // 8: "nodesc" by default
    virtual void Load(void* stream);                   // 9
    virtual void Save(void* stream);                   // 10
    virtual const char* GetClassName();                // 11: e.g. "CTexture"
    virtual int GetClassId();                          // 12: e.g. 6 for CTexture

    // Non-virtual helpers (object.cpp).
    void* ManagerContext() const;   // 00455940: the owning manager's +0xAC
    void* SetOwner(void* owner);    // 00455970: returns the previous owner
    void DestructBase();            // 00455930: base destructor body

    int refCount_;          // +0x04
    std::uint32_t flags_;   // +0x08
    int id_;                // +0x0C
    int type_;              // +0x10 class-specific kind (texture usage for CTexture)
    void* owner_;           // +0x14 manager that owns the object
    std::uint32_t unknown_18;
};
static_assert(sizeof(CEngineObject) == 0x1C);
