#pragma once

// The world server's manager of server-side state objects: CServerSpecificManager
// (class id 0x31; created by the world server) and the torch state object it makes for
// type 0x4B, CTorchObjectSpec (0x58 bytes, class id 0x42). Both are real virtual classes: the
// compiler makes their vtables).
// server_specific_manager.cpp. Names from the classes' own GetClassName strings.

#include "base_manager.h"
#include "engine_object.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;   // core_streams.h
class CMapObject;   // map_object.h
struct DeltaField;  // net_delta.h

class CServerSpecificManager : public CBaseManager {
public:
    // CBaseManager's constructor.
    CServerSpecificManager();
    // CBaseManager's destructor.
    ~CServerSpecificManager() override;

    // 11: "CServerSpecificManager" (the exe's string).
    const char* GetClassName() override;
    // 12: 0x31.
    int GetClassId() override;
    // A new object of `type`: 0x46 CObject3DServer (0x20 bytes),
    // 0x47 CMapObjectServer (0x94), 0x48 CUnitServer (0xB20), 0x49 the player's server
    // object (0x5128), 0x4B CTorchObjectSpec (constructed inline, then its Create);
    // null for any other type (0x4A too) or when the allocation fails.
    CEngineObject* NewObject(int type) override;

};
static_assert(sizeof(CServerSpecificManager) == 0xB0);

class CTorchObjectSpec : public CEngineObject {
public:
    // The engine object's fields, an empty name, no torch, then CreateSpec (the manager's
    // NewObject constructs it inline and calls Create; the client half's constructor
    // calls CreateSpec).
    CTorchObjectSpec();
    // DestroySpec (the game's Destroy through the vtable), the name, the engine object.
    ~CTorchObjectSpec() override;
    // The deleting destructor (slot 0) frees into the game's heap.

    int Create() override { return CreateSpec(); }
    void Destroy() override { DestroySpec(); }
    int RunTick() override;                          // 6: returns 1
    void Load(CStream* stream) override;                // 9: LoadSpec
    void Save(CStream* stream) override;                // 10: SaveSpec
    // 11: "CTorchObjectSpec". 12: 0x42.
    const char* GetClassName() override;
    int GetClassId() override;
    // 13-15: empty here; the client half (torch_client_spec.h) shows its
    // effects, hides them and warms its particles up.
    virtual void ShowEffectsSlot() {}       // 13
    virtual void HideEffectsSlot() {}       // 14
    virtual void WarmUpParticlesSlot() {}   // 15
    // 16-18: the setters below; the client half rebuilds its effects there. Callers reach them
    // through the vtable (the network read, the world server's CreateObject).
    virtual void SetLightSlot(std::uint32_t kind, const float* colour, float value) { SetLight(kind, colour, value); }
    virtual void SetFlameSlot(const float* colour, float value, const float* values) { SetFlame(colour, value, values); }
    virtual void SetNameSlot(const char* name) { SetName(name); }

    // torch_spec.cpp. Slots 2 / 3 (other names so they are no
    // overrides): Create clears +0x1C / +0x20 and the name, values 1, 10, 1, 1, 1, 1, both colours
    // the default colour (black), returns 1; Destroy clears +0x1C and the name. Slots 9 / 10:
    // +0x20, the name (written with its terminator), +0x28, +0x2C, the
    // four values +0x30 and both colours. Slots 16-18: set +0x20, the first
    // colour and +0x28; the second colour, +0x2C and the four values; the name.
    int CreateSpec();
    void DestroySpec();
    void LoadSpec(CStream* stream);
    void SaveSpec(CStream* stream);
    void SetLight(std::uint32_t kind, const float* colour, float value);
    void SetFlame(const float* colour, float value, const float* values);
    void SetName(const char* name);
    // torch_spec.cpp: the torch's network read. Reads the kind,
    // values_[0], values_[1], values_[2..5], both vectors and the name from `field`, then applies them
    // through the vtable (slots 16, 17, 18; a client half rebuilds its effects there), shows the
    // effects (13) and warms the particles up (15).
    void ReadNetState(DeltaField* field);

    CMapObject* torch_;         // the torch it belongs to (set by the torch's attach); null when constructed
    std::uint32_t lightKind_;   // +0x20 the light's kind (SetLight; saved and sent)
    mfc::CString name_;         // +0x24
    float values_[6];           // +0x28 Create: 1, 10, 1, 1, 1, 1
    float colours_[2][3];       // +0x40 Create: both black
};
static_assert(offsetof(CTorchObjectSpec, torch_) == 0x1C);
static_assert(offsetof(CTorchObjectSpec, name_) == 0x24);
static_assert(offsetof(CTorchObjectSpec, colours_) == 0x40);
static_assert(sizeof(CTorchObjectSpec) == 0x58);
