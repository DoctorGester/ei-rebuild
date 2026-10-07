#pragma once

// The world server's manager of server-side state objects (A-S7): CServerSpecificManager
// (004D6900-004D6BA0, vtable 0073D4F4, class id 0x31; created by 0058AE10) and the torch state
// object it makes for type 0x4B, CTorchObjectSpec (vtable 0073D558, 0x58 bytes, class id 0x42).
// server_specific_manager.cpp. Names from the classes' own GetClassName strings.

#include "base_manager.h"
#include "engine_object.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CStream;   // core_streams.h

class CServerSpecificManager : public CBaseManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D4F4);

    // 11: 004D6920 "CServerSpecificManager" (the exe's string, 0078DAD0).
    const char* GetClassName() override;
    // 12: 004D6930 (0x31).
    int GetClassId() override;
    // 14: 004D6970 (RET 4). A new object of `type`: 0x46 CObject3DServer (0x20 bytes, 0051F240),
    // 0x47 CMapObjectServer (0x94), 0x48 CUnitServer (0xB20, 00545580), 0x49 the player's server
    // object (0x5128, 0066C540), 0x4B CTorchObjectSpec (constructed inline, then its Create);
    // null for any other type (0x4A too) or when the allocation fails.
    CEngineObject* NewObject(int type) override;

    // 004D6900 constructor body (returns this), 004D6960 destructor body, 004D6940 slot 0's scalar
    // deleting destructor (returns this).
    CServerSpecificManager* Construct();
    void Destruct();
    CServerSpecificManager* DeletingDestruct(unsigned flags);
};
static_assert(sizeof(CServerSpecificManager) == 0xB0);

class CTorchObjectSpec : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D558);

    // Slots 2 / 3: CreateSpec / DestroySpec below (torch_spec.cpp).
    // 11: 004D6B60 "CTorchObjectSpec" (0078DAE8). 12: 004D6B70 (0x42).
    const char* GetClassName() override;
    int GetClassId() override;

    // 004D6BA0 destructor body (Destroy, the name, the engine object), 004D6B80 slot 0's scalar
    // deleting destructor (returns this).
    void Destruct();
    CTorchObjectSpec* DeletingDestruct(unsigned flags);

    // torch_spec.cpp (A-U6). Slots 2 / 3 (0052C610 / 0052C680; other names so they are no
    // overrides): Create clears +0x1C / +0x20 and the name, values 1, 10, 1, 1, 1, 1, both colours
    // the global 007AF2D0, returns 1; Destroy clears +0x1C and the name. Slots 9 / 10 (0052C490 /
    // 0052C500, RET 4): +0x20, the name (004E5840; written with its terminator), +0x28, +0x2C, the
    // four values +0x30 and both colours. Slots 16-18 (RET 0xC / 0xC / 4): set +0x20, the first
    // colour and +0x28; the second colour, +0x2C and the four values; the name.
    int CreateSpec();
    void DestroySpec();
    void LoadSpec(CStream* stream);
    void SaveSpec(CStream* stream);
    void SetLight(std::uint32_t kind, const float* colour, float value);
    void SetFlame(const float* colour, float value, const float* values);
    void SetName(const char* name);

    std::uint32_t unknown_1C;   // +0x1C 0 when constructed
    std::uint32_t unknown_20;   // +0x20
    mfc::CString name_;         // +0x24
    float values_[6];           // +0x28 Create: 1, 10, 1, 1, 1, 1
    float colours_[2][3];       // +0x40 Create: both 007AF2D0..D8
};
static_assert(offsetof(CTorchObjectSpec, unknown_1C) == 0x1C);
static_assert(offsetof(CTorchObjectSpec, name_) == 0x24);
static_assert(offsetof(CTorchObjectSpec, colours_) == 0x40);
static_assert(sizeof(CTorchObjectSpec) == 0x58);
