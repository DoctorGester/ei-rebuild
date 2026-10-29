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
constexpr int kTorchField = 4;
} // namespace

TorchObject::TorchObject() { spec = nullptr; }

TorchObject::~TorchObject() {
    if (spec) spec->Release();
}

void TorchObject::DestroyTorch() {
    TorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    netState.Reset();
    DestroyMapObject();
}

void TorchObject::LoadTorch(ByteStream* stream) {
    LoadMapObject(stream);
    spec->Load(stream);
}

void TorchObject::SaveTorch(ByteStream* stream) {
    SaveMapObject(stream);
    spec->Save(stream);
}

void TorchObject::Load(ByteStream* stream) { LoadTorch(stream); }
void TorchObject::Save(ByteStream* stream) { SaveTorch(stream); }

void TorchObject::AttachTorchObject(EngineObject* object) {
    if (SideTypeOf(object) != SideObjectType::Torch) {
        CMapObject::AttachObject(object);
        return;
    }
    TorchObjectSpec* old = spec;
    spec = static_cast<TorchObjectSpec*>(object);
    if (spec) spec->AddRef();
    if (old) old->Release();
    if (spec) spec->torch = this;    // the spec's owner
}

void TorchObject::WriteTorchNetState() {
    CMapObject::WriteNetState();
    DeltaField* field = netState.BeginField(kTorchField);
    if (!field) return;
    std::uint32_t value = spec->lightKind;
    field->WriteDelta(&value, 4);
    for (float v : {spec->values[0], spec->values[1]}) field->WriteDelta(&v, 4);
    field->WriteDelta(&spec->values[2], 0x10);
    field->WriteDelta(spec->colours[0], 0xC);
    field->WriteDelta(spec->colours[1], 0xC);
    field->WriteStringBoth(spec->name);
    field->EndField();
}

std::uint32_t TorchObject::ReadTorchNetState(int first) {
    std::uint32_t complete = CMapObject::ReadNetState(first);
    DeltaField* field = netState.BeginField(kTorchField);
    if (!field) return complete;
    spec->ReadNetState(field);
    std::uint32_t readTo = delta::g_readSnapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    return complete;
}

const char* TorchObject::ClassName() { return kTorchClassName; }
ClassId TorchObject::GetClassId() { return ClassId::TorchObject; }
