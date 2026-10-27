#pragma once

// Torches (GetClassName "CTorchObject", class id 0x57): a map
// object holding its light / flame description, a CTorchObjectSpec (server_specific_manager.h,
// type_ 0x4B),. Names provisional; torch_object.cpp; Create is in
// map_object_create.cpp. A real virtual class: the compiler makes its vtable.

#include "map_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CTorchObjectSpec;

class CTorchObject : public CMapObject {
public:
    CTorchObject();
    // The spec released (the pointer kept), then the map object's destructor.
    ~CTorchObject() override;

    int Create() override { return CreateTorch(); }
    void Destroy() override { DestroyTorch(); }
    int RunTick() override { return UpdateTorch(); }
    void Load(CStream* stream) override;                      // LoadTorch
    void Save(CStream* stream) override;                      // SaveTorch
    const char* GetClassName() override;                   // "CTorchObject"
    int GetClassId() override;                             // 0x57
    void AttachObject(CEngineObject* object) override { AttachTorchObject(object); }
    bool IsSynchronised() override { return Slot29True(); }
    std::uint32_t ReadNetState(int first) override { return ReadTorchNetState(first); }
    void WriteNetState() override { WriteTorchNetState(); }

    // map_object_create.cpp. Slot 2 (Create): the map object's Create,
    // the spec released and cleared, 5 network fields (SetSize inlined), then
    // DeltaState::Reset. Returns 1.
    int CreateTorch();
    // Slot 3 (Destroy). The spec released and cleared, the network state reset
    // then the map object's Destroy.
    void DestroyTorch();
    // Returns 1 (no map object update).
    int UpdateTorch();
    // Slots 9 / 10. The map object part, then the spec's Load / Save
    // (its slots 9 / 10; the spec must exist).
    void LoadTorch(CStream* stream);
    void SaveTorch(CStream* stream);
    // A type 0x4B object (its type_ read before any null test) becomes
    // the spec (a reference taken, the old one released) and points back here. Anything else goes to the map
    // object's AttachObject.
    void AttachTorchObject(CEngineObject* object);
    // The map object's network state, then field 4: the spec's +0x20, +0x28,
    // +0x2C (through stack copies), +0x30 (16 bytes), the two colours and its name (both channels).
    void WriteTorchNetState();
    // The map object's reader, then field 4 read into the spec
    // (the spec's own reader, which applies it through the spec's slots); the result and-ed with "the snapshot
    // was consumed".
    std::uint32_t ReadTorchNetState(int first);
    // AL = 1.
    bool Slot29True();

    CTorchObjectSpec* spec;         // a counted reference
};

// Slots 11 / 12: "CTorchObject" and 0x57.
const char* __fastcall TorchClassName(CTorchObject* self);
int __fastcall TorchClassId(CTorchObject* self);
