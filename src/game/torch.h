#pragma once

// Torches (ClassName "TorchObject", class id 0x57): a map
// object holding its light / flame description, a TorchObjectSpec (server_specific_manager.h,
// type 0x4B),. Names provisional; torch_object.cpp; Create is in
// map_object.cpp. A real virtual class: the compiler makes its vtable.

#include "game/map_object.h"

#include <cstddef>
#include <cstdint>

class TorchObjectSpec;

class TorchObject : public MapObject {
public:
    TorchObject();
    // The spec released, then the map object's destructor.
    ~TorchObject() override;

    // The map object's Create, the spec released and cleared, 5 network fields (SetSize inlined), then
    // DeltaState::Reset (map_object.cpp).
    void Create() override;
    // The spec released and cleared, the network state reset, then the map object's Destroy.
    void Destroy() override;
    bool RunTick() override { return true; }   // no map object update
    // The map object part, then the spec's (which must exist).
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;                         // "TorchObject"
    ClassId GetClassId() override;
    // A SideObjectType::Torch object (its type read before any null test) becomes the spec; anything else goes to
    // the map object's AttachObject.
    void AttachObject(EngineObject* object) override;
    bool IsSynchronised() override { return true; }
    // The map object's network state, then field 4: the spec's light kind, two values, four more, the two colours
    // and its name (both channels).
    void WriteNetState() override;
    // The map object's reader, then field 4 read into the spec (the spec's own reader, which applies it through
    // the spec's slots); the result and-ed with "the snapshot was consumed".
    bool ReadNetState(bool update) override;

    // `spec` (or null) replaces the spec: a reference taken, the old one released; the new one points back here.
    void SetSpec(TorchObjectSpec* spec);

    Ref<TorchObjectSpec> spec;
};

