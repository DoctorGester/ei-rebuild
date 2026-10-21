// The torch map object (0052BEE0-0052C2C0, A-U6): lifetime, update, save / load, its spec and the
// network state. See torch.h and docs/notes/units.md.
#include "torch.h"

#include "core_streams.h"
#include "net_delta.h"
#include "server_specific_manager.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_units.h"
#include "game_data.h"

#include <initializer_list>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
const char* const kTorchClassName = "CTorchObject";
constexpr int kClassTorch = 0x57;
constexpr int kTypeTorchSpec = 0x4B;
constexpr int kTorchField = 4;
} // namespace

CTorchObject::CTorchObject() { spec = nullptr; }

CTorchObject* CTorchObject::Construct() { return new (this) CTorchObject(); }
EI_REPLACE_METHOD(0x0052BEE0, CTorchObject::Construct);

CTorchObject::~CTorchObject() {
    if (spec) spec->Release();
}

void CTorchObject::DestructBody() { this->CTorchObject::~CTorchObject(); }
EI_REPLACE_METHOD(0x0052BF60, CTorchObject::DestructBody);

CTorchObject* CTorchObject::DeletingDestruct(int flags) {
    this->CTorchObject::~CTorchObject();
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

void CTorchObject::Load(void* stream) { LoadTorch(static_cast<CStream*>(stream)); }
void CTorchObject::Save(void* stream) { SaveTorch(static_cast<CStream*>(stream)); }

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
    spec->ReadNetState(field);
    std::uint32_t readTo = delta::ReadSnapshot()->Position();
    std::uint32_t size = delta::ReadSnapshot()->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    return complete;
}
EI_REPLACE_METHOD(0x0052C220, CTorchObject::ReadTorchNetState);

const char* CTorchObject::GetClassName() { return kTorchClassName; }
int CTorchObject::GetClassId() { return kClassTorch; }

const char* __fastcall TorchClassName(CTorchObject* self) { return self->CTorchObject::GetClassName(); }
EI_REPLACE(0x0052BF20, TorchClassName);

int __fastcall TorchClassId(CTorchObject* self) { return self->CTorchObject::GetClassId(); }
EI_REPLACE(0x0052BF30, TorchClassId);
