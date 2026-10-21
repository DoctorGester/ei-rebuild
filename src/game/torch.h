#pragma once

// Torches (GetClassName "CTorchObject", class id 0x57, 0x23C bytes, constructor 0052BEE0): a map
// object holding its light / flame description, a CTorchObjectSpec (server_specific_manager.h,
// type_ 0x4B), at +0x238. Names provisional; torch_object.cpp (A-U6); Create (0052BFC0) is in
// map_object_create.cpp. A real virtual class: the compiler makes its vtable (C19; game.exe's
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
    // 0052BEE0. The map object's constructor, no spec.
    CTorchObject();
    // 0052BF60. The spec released (the pointer kept), then the map object's destructor.
    ~CTorchObject() override;
    // The deleting destructor (slot 0, 0052BF40) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    int Create() override { return CreateTorch(); }        // 2: 0052BFC0
    void Destroy() override { DestroyTorch(); }            // 3: 0052C150
    int RunTick() override { return UpdateTorch(); }       // 6: 0052BF10
    void Load(CStream* stream) override;                      // 9: 0052C280 LoadTorch
    void Save(CStream* stream) override;                      // 10: 0052C2A0 SaveTorch
    const char* GetClassName() override;                   // 11: 0052BF20 "CTorchObject"
    int GetClassId() override;                             // 12: 0052BF30 (0x57)
    std::uintptr_t AttachObject(CEngineObject* object) override { return AttachTorchObject(object); }   // 19
    bool IsSynchronised() override { return Slot29True(); }                         // 29
    std::uint32_t ReadNetState(int first) override { return ReadTorchNetState(first); }   // 49
    void WriteNetState() override { WriteTorchNetState(); }                         // 50

    // The registrations of the constructor and destructors (C20 retires them): 0052BEE0 the
    // constructor in place (returns this), 0052BF60 the destructor, 0052BF40 (slot 0, RET 4) the
    // destructor, then operator delete when `flags` bit 0 is set (returns this).
    CTorchObject* Construct();
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
