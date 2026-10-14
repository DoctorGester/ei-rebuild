#pragma once

// Torches (GetClassName "CTorchObject", class id 0x57, 0x23C bytes, vtable 0073E834, constructor
// 0052BEE0): a map object holding its light / flame description, a CTorchObjectSpec
// (server_specific_manager.h, type_ 0x4B), at +0x238. Names provisional; torch_object.cpp (A-U6);
// Create (0052BFC0) is in map_object_create.cpp.

#include "map_object.h"

#include <cstddef>
#include <cstdint>

class CStream;            // core_streams.h
class CTorchObjectSpec;   // server_specific_manager.h

class CTorchObject : public CMapObject {
public:
    // 0052BEE0. The map object's constructor, no spec, the vtable. Returns this.
    CTorchObject* Construct();
    // 0052BF60. The vtable, the spec released (the pointer kept), the map object's destructor body.
    // 0052BF40 (slot 0, RET 4): it, then operator delete when `flags` bit 0 is set; returns this.
    void DestructBody();
    CTorchObject* DeletingDestruct(int flags);
    // map_object_create.cpp (U6). 0052BFC0 (slot 2, Create): the map object's Create (0050E770),
    // the spec released and cleared, 5 network fields (SetSize 0050FA30 inlined), then
    // DeltaState::Reset. Returns 1.
    int CreateTorch();
    // 0052C150 (slot 3, Destroy). The spec released and cleared, the network state reset
    // (0047FB70), then the map object's Destroy.
    void DestroyTorch();
    // 0052BF10 (slot 6). Returns 1 (no map object update).
    int UpdateTorch();
    // 0052C280 / 0052C2A0 (slots 9 / 10, RET 4). The map object part, then the spec's Load / Save
    // (its slots 9 / 10; the spec must exist).
    void LoadTorch(CStream* stream);
    void SaveTorch(CStream* stream);
    // 0052C2C0 (slot 19, RET 4). A type 0x4B object (its type_ read before any null test) becomes
    // the spec (a reference taken, the old one released) and points back here (+0x1C); returns
    // the new spec. Anything else goes to the map object's AttachObject (0050E890), whose EAX it
    // returns.
    std::uintptr_t AttachTorchObject(CEngineObject* object);
    // 0052C180 (slot 50). The map object's network state, then field 4: the spec's +0x20, +0x28,
    // +0x2C (through stack copies), +0x30 (16 bytes), the two colours and its name (both channels).
    void WriteTorchNetState();
    // 0052C220 (slot 49, RET 4). The map object's reader, then field 4 read into the spec
    // (0052C390, which applies it through the spec's slots); the result and-ed with "the snapshot
    // was consumed".
    std::uint32_t ReadTorchNetState(int first);
    // 0052BF00 (slot 29): AL = 1.
    bool Slot29True();

    CTorchObjectSpec* spec;         // +0x238 a counted reference
};
static_assert(offsetof(CTorchObject, spec) == 0x238);
static_assert(sizeof(CTorchObject) == 0x23C);

// 0052BF20 / 0052BF30 (slots 11 / 12): "CTorchObject" (0078F798) and 0x57.
const char* __fastcall TorchClassName(CTorchObject* self);
int __fastcall TorchClassId(CTorchObject* self);
