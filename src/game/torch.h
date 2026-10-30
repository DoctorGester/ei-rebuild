#pragma once

// Torches (ClassName "TorchObject", class id 0x57): a map
// object holding its light / flame description, a TorchObjectSpec (server_specific_manager.h,
// type 0x4B),. Names provisional; torch_object.cpp; Create is in
// map_object_create.cpp. A real virtual class: the compiler makes its vtable.

#include "map_object.h"

#include <cstddef>
#include <cstdint>

class TorchObjectSpec;

class TorchObject : public MapObject {
public:
    TorchObject();
    // The spec released (the pointer kept), then the map object's destructor.
    ~TorchObject() override;

    // The map object's Create, the spec released and cleared, 5 network fields (SetSize inlined), then
    // DeltaState::Reset (map_object_create.cpp).
    void Create() override;
    void Destroy() override { DestroyTorch(); }
    bool RunTick() override { return true; }   // no map object update
    void Load(ByteStream* stream) override;                      // LoadTorch
    void Save(ByteStream* stream) override;                      // SaveTorch
    const char* ClassName() override;                         // "TorchObject"
    ClassId GetClassId() override;
    void AttachObject(EngineObject* object) override { AttachTorchObject(object); }
    bool IsSynchronised() override { return true; }
    std::uint32_t ReadNetState(int first) override { return ReadTorchNetState(first); }
    void WriteNetState() override { WriteTorchNetState(); }

    // map_object_create.cpp.
    // Destroy. The spec released and cleared, the network state reset
    // then the map object's Destroy.
    void DestroyTorch();
    // Load / Save. The map object part, then the spec's Load / Save
    // (its Load / Save; the spec must exist).
    void LoadTorch(ByteStream* stream);
    void SaveTorch(ByteStream* stream);
    // A SideObjectType::Torch object (its type read before any null test) becomes
    // the spec (a reference taken, the old one released) and points back here. Anything else goes to the map
    // object's AttachObject.
    void AttachTorchObject(EngineObject* object);
    // The map object's network state, then field 4: the spec's +0x20, +0x28,
    // +0x2C (through stack copies), +0x30 (16 bytes), the two colours and its name (both channels).
    void WriteTorchNetState();
    // The map object's reader, then field 4 read into the spec
    // (the spec's own reader, which applies it through the spec's slots); the result and-ed with "the snapshot
    // was consumed".
    std::uint32_t ReadTorchNetState(int first);

    TorchObjectSpec* spec;         // a counted reference
};

