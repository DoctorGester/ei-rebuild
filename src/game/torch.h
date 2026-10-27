#pragma once

// Torches (GetClassName "CTorchObject", class id 0x57, 0x23C bytes, constructor 0052BEE0): a map
// object holding its light / flame description, a CTorchObjectSpec (server_specific_manager.h,
// type_ 0x4B), at +0x238. Names provisional; torch_object.cpp (A-U6); Create is in
// map_object_create.cpp. A real virtual class: the compiler makes its vtable (game.exe's
// 0073E834 and its tail 0073E850 are retired).

#include "map_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;            // core_streams.h
class CTorchObjectSpec;   // server_specific_manager.h

class CTorchObject : public CMapObject {
public:
    // The map object's constructor, no spec.
    CTorchObject();
    // The spec released (the pointer kept), then the map object's destructor.
    ~CTorchObject() override;
    // The deleting destructor (slot 0, 0052BF40) frees into the game's heap.

    int Create() override { return CreateTorch(); }
    void Destroy() override { DestroyTorch(); }
    int RunTick() override { return UpdateTorch(); }
    void Load(CStream* stream) override;                      // 9: 0052C280 LoadTorch
    void Save(CStream* stream) override;                      // 10: 0052C2A0 SaveTorch
    const char* GetClassName() override;                   // 11: 0052BF20 "CTorchObject"
    int GetClassId() override;                             // 12: 0052BF30 (0x57)
    std::uintptr_t AttachObject(CEngineObject* object) override { return AttachTorchObject(object); }   // 19
    bool IsSynchronised() override { return Slot29True(); }                         // 29
    std::uint32_t ReadNetState(int first) override { return ReadTorchNetState(first); }   // 49
    void WriteNetState() override { WriteTorchNetState(); }                         // 50

    // map_object_create.cpp. 0052BFC0 (slot 2, Create): the map object's Create,
    // the spec released and cleared, 5 network fields (SetSize 0050FA30 inlined), then
    // DeltaState::Reset. Returns 1.
    int CreateTorch();
    // 0052C150 (slot 3, Destroy). The spec released and cleared, the network state reset
    // then the map object's Destroy.
    void DestroyTorch();
    // Returns 1 (no map object update).
    int UpdateTorch();
    // 0052C280 / 0052C2A0 (slots 9 / 10, RET 4). The map object part, then the spec's Load / Save
    // (its slots 9 / 10; the spec must exist).
    void LoadTorch(CStream* stream);
    void SaveTorch(CStream* stream);
    // A type 0x4B object (its type_ read before any null test) becomes
    // the spec (a reference taken, the old one released) and points back here (+0x1C); returns
    // the new spec. Anything else goes to the map object's AttachObject, whose EAX it
    // returns.
    std::uintptr_t AttachTorchObject(CEngineObject* object);
    // The map object's network state, then field 4: the spec's +0x20, +0x28,
    // +0x2C (through stack copies), +0x30 (16 bytes), the two colours and its name (both channels).
    void WriteTorchNetState();
    // The map object's reader, then field 4 read into the spec
    // (0052C390, which applies it through the spec's slots); the result and-ed with "the snapshot
    // was consumed".
    std::uint32_t ReadTorchNetState(int first);
    // AL = 1.
    bool Slot29True();

    CTorchObjectSpec* spec;         // +0x238 a counted reference
};
static_assert(offsetof(CTorchObject, spec) == 0x238);
static_assert(sizeof(CTorchObject) == 0x23C);

// 0052BF20 / 0052BF30 (slots 11 / 12): "CTorchObject" and 0x57.
const char* __fastcall TorchClassName(CTorchObject* self);
int __fastcall TorchClassId(CTorchObject* self);
