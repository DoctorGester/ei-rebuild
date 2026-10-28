// The torch's state object, CTorchObjectSpec (server_specific_manager.h): its Create / Destroy,
// Load / Save and setter slots. The network reader stays
// the game's own code (it applies the values through its virtual setters, which a client-side spec may override).
#include "server_specific_manager.h"

#include "core_streams.h"
#include "net_delta.h"

#include <bit>
#include <cstring>
#include <string>

namespace {
constexpr float kDefaultValue = 1.0f;
constexpr float kDefaultValue2C = 10.0f;
// The default colour both colours start from (a static initialiser: black).
constexpr float kDefaultColour[3] = /*007AF2D0*/ {};
} // namespace

int CTorchObjectSpec::CreateSpec() {
    torch_ = nullptr;
    lightKind_ = 0;
    name_.clear();
    values_[1] = kDefaultValue2C;
    values_[0] = kDefaultValue;
    for (int i = 2; i < 6; ++i) values_[i] = kDefaultValue;
    std::memcpy(colours_[0], kDefaultColour, sizeof(colours_[0]));
    std::memcpy(colours_[1], kDefaultColour, sizeof(colours_[1]));
    return 1;
}

void CTorchObjectSpec::DestroySpec() {
    torch_ = nullptr;
    name_.clear();
}

void CTorchObjectSpec::LoadSpec(CStream* stream) {
    stream->Read(&lightKind_, 4);
    stream->ReadInto(&name_);
    stream->Read(&values_[0], 4);
    stream->Read(&values_[1], 4);
    stream->Read(&values_[2], 0x10);
    stream->Read(colours_[0], 0xC);
    stream->Read(colours_[1], 0xC);
}

void CTorchObjectSpec::SaveSpec(CStream* stream) {
    stream->Write(&lightKind_, 4);
    stream->WriteString(name_);
    std::uint32_t value;
    value = std::bit_cast<std::uint32_t>(values_[0]);
    stream->Write(&value, 4);
    value = std::bit_cast<std::uint32_t>(values_[1]);
    stream->Write(&value, 4);
    stream->Write(&values_[2], 0x10);
    stream->Write(colours_[0], 0xC);
    stream->Write(colours_[1], 0xC);
}

void CTorchObjectSpec::Load(CStream* stream) { LoadSpec(stream); }
void CTorchObjectSpec::Save(CStream* stream) { SaveSpec(stream); }

void CTorchObjectSpec::SetLight(std::uint32_t kind, const float* colour, float value) {
    lightKind_ = kind;
    std::memcpy(colours_[0], colour, sizeof(colours_[0]));
    values_[0] = value;
}

void CTorchObjectSpec::SetFlame(const float* colour, float value, const float* values) {
    std::memcpy(colours_[1], colour, sizeof(colours_[1]));
    values_[1] = value;
    std::memcpy(&values_[2], values, 4 * sizeof(float));
}

void CTorchObjectSpec::SetName(const char* name) { name_ = name; }

void CTorchObjectSpec::ReadNetState(DeltaField* field) {
    std::uint32_t kind;
    field->ReadDelta(&kind, 4);
    lightKind_ = kind;
    field->ReadDelta(&values_[0], 4);
    field->ReadDelta(&values_[1], 4);
    field->ReadDelta(&values_[2], 0x10);
    field->ReadDelta(colours_[0], 0xC);
    field->ReadDelta(colours_[1], 0xC);
    {
        std::string read;
        name_ = *field->ReadString(&read);
    }
    SetLightSlot(lightKind_, colours_[0], values_[0]);
    SetFlameSlot(colours_[1], values_[1], &values_[2]);
    SetNameSlot(name_.c_str());
    ShowEffectsSlot();
    WarmUpParticlesSlot();
}
