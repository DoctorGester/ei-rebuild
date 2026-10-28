#pragma once

// The world server's manager of server-side state objects: CServerSpecificManager
// (class id 0x31; created by the world server) and the torch state object it makes for
// type 0x4B, CTorchObjectSpec (0x58 bytes, class id 0x42). Both are real virtual classes: the
// compiler makes their vtables).
// server_specific_manager.cpp. Names from the classes' own GetClassName strings.

#include "base_manager.h"
#include "engine_object.h"
#include <cstddef>
#include <cstdint>
#include <string>


#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;
class CMapObject;
struct DeltaField;

class CServerSpecificManager : public CBaseManager {
public:
    CServerSpecificManager();
    ~CServerSpecificManager() override;

    // "CServerSpecificManager" (the exe's string).
    const char* GetClassName() override;
    // 0x31.
    int GetClassId() override;
    // A new object of `type`: 0x46 CObject3DServer,
    // 0x47 CMapObjectServer (0x94), 0x48 CUnitServer (0xB20), 0x49 the player's server
    // object (0x5128), 0x4B CTorchObjectSpec (constructed inline, then its Create);
    // null for any other type (0x4A too) or when the allocation fails.
    CEngineObject* NewObject(int type) override;
};

class CTorchObjectSpec : public CEngineObject {
public:
    CTorchObjectSpec();
    ~CTorchObjectSpec() override;

    int Create() override { return CreateSpec(); }
    void Destroy() override { DestroySpec(); }
    int RunTick() override;                          // returns 1
    void Load(CStream* stream) override;                // LoadSpec
    void Save(CStream* stream) override;                // SaveSpec
    // "CTorchObjectSpec". 12: 0x42.
    const char* GetClassName() override;
    int GetClassId() override;
    // 13-15: empty here; the client half (torch_client_spec.h) shows its
    // effects, hides them and warms its particles up.
    virtual void ShowEffectsSlot() {}
    virtual void HideEffectsSlot() {}
    virtual void WarmUpParticlesSlot() {}
    // 16-18: the setters below; the client half rebuilds its effects there. Callers reach them
    // through the vtable (the network read, the world server's CreateObject).
    virtual void SetLightSlot(std::uint32_t kind, const float* colour, float value) { SetLight(kind, colour, value); }
    virtual void SetFlameSlot(const float* colour, float value, const float* values) { SetFlame(colour, value, values); }
    virtual void SetNameSlot(const char* name) { SetName(name); }

    // torch_spec.cpp. Create / Destroy (other names so they are no
    // overrides): Create clears +0x1C / +0x20 and the name, values 1, 10, 1, 1, 1, 1, both colours
    // the default colour (black), returns 1; Destroy clears +0x1C and the name. Load / Save:
    // +0x20, the name (written with its terminator), +0x28, +0x2C, the
    // four values +0x30 and both colours. The setters: set +0x20, the first
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
    // through the virtual setters (a client half rebuilds its effects there), shows the
    // effects and warms the particles up.
    void ReadNetState(DeltaField* field);

    CMapObject* torch_;         // the torch it belongs to (set by the torch's attach); null when constructed
    std::uint32_t lightKind_;   // the light's kind (SetLight; saved and sent)
    std::string name_;
    float values_[6];           // Create: 1, 10, 1, 1, 1, 1
    float colours_[2][3];       // Create: both black
};
