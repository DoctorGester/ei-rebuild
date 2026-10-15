// The torch map object (0052BEE0-0052C2C0, A-U6): lifetime, update, save / load, its spec and the
// network state. See torch.h and docs/notes/units.md.
#include "torch.h"

#include "core_streams.h"
#include "net_delta.h"
#include "server_specific_manager.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_units.h"

#include <initializer_list>

namespace {
void* const kTorchVtable = reinterpret_cast<void*>(0x0073E834);
const char* const kTorchClassName = reinterpret_cast<const char*>(0x0078F798);   // "CTorchObject"
constexpr int kClassTorch = 0x57;
constexpr int kTypeTorchSpec = 0x4B;
constexpr int kTorchField = 4;

void SetVtable(CTorchObject* torch) { *reinterpret_cast<void**>(torch) = kTorchVtable; }
} // namespace

CTorchObject* CTorchObject::Construct() {
    CMapObject::Construct();
    spec = nullptr;
    SetVtable(this);
    return this;
}
EI_REPLACE_METHOD(0x0052BEE0, CTorchObject::Construct);

void CTorchObject::DestructBody() {
    SetVtable(this);
    if (spec) spec->Release();
    CMapObject::DestructBody();
}
EI_REPLACE_METHOD(0x0052BF60, CTorchObject::DestructBody);

CTorchObject* CTorchObject::DeletingDestruct(int flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0052BF40, CTorchObject::DeletingDestruct);

void CTorchObject::DestroyTorch() {
    CTorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    NetState()->Reset();
    DestroyMapObject();
}
EI_REPLACE_METHOD(0x0052C150, CTorchObject::DestroyTorch);

int CTorchObject::UpdateTorch() { return 1; }
EI_REPLACE_METHOD(0x0052BF10, CTorchObject::UpdateTorch);

bool CTorchObject::Slot29True() { return true; }
EI_REPLACE_METHOD(0x0052BF00, CTorchObject::Slot29True);

void CTorchObject::LoadTorch(CStream* stream) {
    LoadMapObject(stream);
    spec->Load(stream);
}
EI_REPLACE_METHOD(0x0052C280, CTorchObject::LoadTorch);

void CTorchObject::SaveTorch(CStream* stream) {
    SaveMapObject(stream);
    spec->Save(stream);
}
EI_REPLACE_METHOD(0x0052C2A0, CTorchObject::SaveTorch);

std::uintptr_t CTorchObject::AttachTorchObject(CEngineObject* object) {
    if (object->type_ != kTypeTorchSpec) return CMapObject::AttachObjectImpl(object);
    CTorchObjectSpec* old = spec;
    spec = static_cast<CTorchObjectSpec*>(object);
    if (spec) ++spec->refCount_;
    if (old) old->Release();
    if (!spec) return 0;
    spec->torch_ = this;   // the spec's owner
    return reinterpret_cast<std::uintptr_t>(spec);
}
EI_REPLACE_METHOD(0x0052C2C0, CTorchObject::AttachTorchObject);

void CTorchObject::WriteTorchNetState() {
    MapObjectWriteNetState(reinterpret_cast<std::uint8_t*>(this));
    DeltaField* field = NetState()->BeginField(kTorchField);
    if (!field) return;
    std::uint32_t value = spec->lightKind_;
    field->WriteDelta(&value, 4);
    for (float v : {spec->values_[0], spec->values_[1]}) field->WriteDelta(&v, 4);
    field->WriteDelta(&spec->values_[2], 0x10);
    field->WriteDelta(spec->colours_[0], 0xC);
    field->WriteDelta(spec->colours_[1], 0xC);
    field->WriteStringBoth(spec->name_);
    field->EndField();
}
EI_REPLACE_METHOD(0x0052C180, CTorchObject::WriteTorchNetState);

std::uint32_t CTorchObject::ReadTorchNetState(int first) {
    std::uint32_t complete = MapObjectReadNetState(reinterpret_cast<std::uint8_t*>(this), nullptr, first);
    DeltaField* field = NetState()->BeginField(kTorchField);
    if (!field) return complete;
    TorchSpecReadNetOriginal(spec, nullptr, field);
    std::uint32_t readTo = delta::ReadSnapshot()->Position();
    std::uint32_t size = delta::ReadSnapshot()->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    return complete;
}
EI_REPLACE_METHOD(0x0052C220, CTorchObject::ReadTorchNetState);

const char* __fastcall TorchClassName(CTorchObject*) { return kTorchClassName; }
EI_REPLACE(0x0052BF20, TorchClassName);

int __fastcall TorchClassId(CTorchObject*) { return kClassTorch; }
EI_REPLACE(0x0052BF30, TorchClassId);
