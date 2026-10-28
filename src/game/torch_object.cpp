// The torch map object: lifetime, update, save / load, its spec and the
// network state. See torch.h and docs/notes/units.md.
#include "core_streams.h"
#include "net_delta.h"
#include "server_specific_manager.h"
#include "torch.h"

#include <initializer_list>
#include <new>

namespace {
const char* const kTorchClassName = "CTorchObject";
constexpr int kTypeTorchSpec = 0x4B;
constexpr int kTorchField = 4;
} // namespace

CTorchObject::CTorchObject() { spec = nullptr; }

CTorchObject::~CTorchObject() {
    if (spec) spec->Release();
}

void CTorchObject::DestroyTorch() {
    CTorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    netState_.Reset();
    DestroyMapObject();
}

int CTorchObject::UpdateTorch() { return 1; }

bool CTorchObject::Slot29True() { return true; }

void CTorchObject::LoadTorch(CStream* stream) {
    LoadMapObject(stream);
    spec->Load(stream);
}

void CTorchObject::SaveTorch(CStream* stream) {
    SaveMapObject(stream);
    spec->Save(stream);
}

void CTorchObject::Load(CStream* stream) { LoadTorch(stream); }
void CTorchObject::Save(CStream* stream) { SaveTorch(stream); }

void CTorchObject::AttachTorchObject(CEngineObject* object) {
    if (object->type_ != kTypeTorchSpec) {
        CMapObject::AttachObject(object);
        return;
    }
    CTorchObjectSpec* old = spec;
    spec = static_cast<CTorchObjectSpec*>(object);
    if (spec) ++spec->refCount_;
    if (old) old->Release();
    if (spec) spec->torch_ = this;   // the spec's owner
}

void CTorchObject::WriteTorchNetState() {
    CMapObject::WriteNetState();
    DeltaField* field = netState_.BeginField(kTorchField);
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

std::uint32_t CTorchObject::ReadTorchNetState(int first) {
    std::uint32_t complete = CMapObject::ReadNetState(first);
    DeltaField* field = netState_.BeginField(kTorchField);
    if (!field) return complete;
    spec->ReadNetState(field);
    std::uint32_t readTo = delta::g_readSnapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    return complete;
}

const char* CTorchObject::ClassName() { return kTorchClassName; }
ClassId CTorchObject::GetClassId() { return kClassTorchObject; }
