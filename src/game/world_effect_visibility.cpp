// CEffectShowVisibility: a unit's sight shown in the fog of war (clairvoyance). See world_effect.h
// and docs/notes/effects.md ("Missiles and visibility").
#include "world_effect.h"

#include "map_object.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "world.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

using ei::SetVtable;

namespace {

// The fog manager (*007AF75C); 004E28D0 (thiscall) makes a fog object, already counted once by
// the manager's list.
void* FogManager() { return *reinterpret_cast<void**>(0x007AF75C); }
__declspec(naked) CEngineObject* __fastcall CreateFog(void* /*manager*/) EI_ORIGINAL(0x004E28D0)

// 005CD210 (fastcall, RET 4): the squares `seer` sees, with `observer`'s skills, into `grid`.
__declspec(naked) void __fastcall ComputeSight(CEngineObject* /*seer*/, CEngineObject* /*observer*/, FogGrid* /*grid*/)
    EI_ORIGINAL(0x005CD210)

// A memory stream on the stack, as the original builds and destroys it inline.
struct TempStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    TempStream() { Get()->Construct(); }
    ~TempStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};

void ReleaseFog(CEngineObject*& fog) {
    CEngineObject* old = fog;
    fog = nullptr;
    if (old != nullptr) old->Release();
}

} // namespace

__declspec(naked) void FogGrid::Clear() EI_ORIGINAL(0x005C8B60)
__declspec(naked) void FogGrid::Read(CMemoryStream*) EI_ORIGINAL(0x005C8BF0)
__declspec(naked) void FogGrid::Write(CMemoryStream*) EI_ORIGINAL(0x005C8CC0)

std::uintptr_t CEffectShowVisibility::Vtable() { return static_cast<std::uintptr_t>(0x0074381C); }

CEffectShowVisibility* CEffectShowVisibility::Construct() {
    ConstructEffect();
    fog_ = nullptr;
    grid_.width = 1;
    grid_.height = 1;
    grid_.cells = static_cast<std::uint8_t*>(gamecrt::operator_new(1));
    grid_.rows = static_cast<std::uint8_t**>(gamecrt::operator_new(grid_.height * 4));
    for (int row = 0; row < grid_.height; ++row) grid_.rows[row] = grid_.cells + row * grid_.width;
    observerId_ = -1;
    seerId_ = -1;
    SetVtable(this, Vtable());
    return this;
}

// The destructor body resets twice (inline, then slot 18), frees the grid and destroys the base.
void CEffectShowVisibility::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();
    gamecrt::operator_delete(grid_.cells);
    gamecrt::operator_delete(grid_.rows);
    DestructEffect();
}

CEffectShowVisibility* CEffectShowVisibility::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// The base is not reset: only the grid (taken out of the fog first), the units and the fog.
void CEffectShowVisibility::Reset() {
    grid_.Clear();
    if (fog_ != nullptr) Fog()->Reveal(&grid_);
    observerId_ = -1;
    seerId_ = -1;
    ReleaseFog(fog_);
}

void CEffectShowVisibility::SetUnits(int observerId, int seerId) {
    observerId_ = observerId;
    seerId_ = seerId;
}

void CEffectShowVisibility::Start(int update) {
    if (update == 0) {
        CEngineObject* created = CreateFog(FogManager());
        CEngineObject* old = fog_;
        fog_ = created;
        if (created != nullptr) ++created->refCount_;
        if (old != nullptr) old->Release();
    }
    if (fog_ != nullptr) Fog()->Reveal(&grid_);
}

int CEffectShowVisibility::Unpack(int update, DeltaField* field) {
    TempStream stream;
    if (field->ReadStream(stream.Get()) == 0) return 0;
    grid_.Read(stream.Get());
    Start(update);
    return 1;
}

void CEffectShowVisibility::Pack(DeltaField* field) {
    TempStream stream;
    grid_.Write(stream.Get());
    field->WriteStreamBoth(stream.Get());
}

int CEffectShowVisibility::Tick() {
    if (!static_cast<CWorld*>(ManagerContext())->IsServer()) return 1;
    CEngineObject* seer = WorldFindUnit(ObjectWorld(this), nullptr, seerId_);
    CEngineObject* observer = WorldFindUnit(ObjectWorld(this), nullptr, observerId_);
    if (seer == nullptr || observer == nullptr) return 0;
    std::memcpy(&localPosition_, &static_cast<CMapObject*>(seer)->positionX, sizeof(Vector3));
    ComputeSight(seer, observer, &grid_);
    MarkChanged();
    return 1;
}

void CEffectShowVisibility::LoadState(void* stream) {
    auto* s = static_cast<CStream*>(stream);
    s->Read(&observerId_, 4);
    s->Read(&seerId_, 4);
}

void CEffectShowVisibility::SaveState(void* stream) {
    auto* s = static_cast<CStream*>(stream);
    int copy = observerId_;
    s->Write(&copy, 4);
    copy = seerId_;
    s->Write(&copy, 4);
}

EI_REPLACE_METHOD(0x0067C340, CEffectShowVisibility::Construct);
EI_REPLACE_METHOD(0x0067C490, CEffectShowVisibility::Destruct);
EI_REPLACE_METHOD(0x0067C470, CEffectShowVisibility::DeletingDestruct);
EI_REPLACE_METHOD(0x0067C5F0, CEffectShowVisibility::Reset);
EI_REPLACE_METHOD(0x0067C640, CEffectShowVisibility::SetUnits);
EI_REPLACE_METHOD(0x0067C660, CEffectShowVisibility::Start);
EI_REPLACE_METHOD(0x0067C6B0, CEffectShowVisibility::Unpack);
EI_REPLACE_METHOD(0x0067C7A0, CEffectShowVisibility::Pack);
EI_REPLACE_METHOD(0x0067C840, CEffectShowVisibility::Tick);
EI_REPLACE_METHOD(0x0067C8E0, CEffectShowVisibility::LoadState);
EI_REPLACE_METHOD(0x0067C910, CEffectShowVisibility::SaveState);
