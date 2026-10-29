// The torch's state object, TorchObjectSpec (server_specific_manager.h): its Create / Destroy,
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

void TorchObjectSpec::CreateSpec() {
    torch = nullptr;
    lightKind = 0;
    name.clear();
    values[1] = kDefaultValue2C;
    values[0] = kDefaultValue;
    for (int i = 2; i < 6; ++i) values[i] = kDefaultValue;
    std::memcpy(colours[0], kDefaultColour, sizeof(colours[0]));
    std::memcpy(colours[1], kDefaultColour, sizeof(colours[1]));
}

void TorchObjectSpec::DestroySpec() {
    torch = nullptr;
    name.clear();
}

void TorchObjectSpec::LoadSpec(ByteStream* stream) {
    stream->ReadValue(lightKind);
    name = stream->ReadString();
    stream->ReadValue(values[0]);
    stream->ReadValue(values[1]);
    stream->Read(&values[2], 0x10);
    stream->Read(colours[0], 0xC);
    stream->Read(colours[1], 0xC);
}

void TorchObjectSpec::SaveSpec(ByteStream* stream) {
    stream->WriteValue(lightKind);
    stream->WriteString(name);
    std::uint32_t value;
    value = std::bit_cast<std::uint32_t>(values[0]);
    stream->WriteValue(value);
    value = std::bit_cast<std::uint32_t>(values[1]);
    stream->WriteValue(value);
    stream->Write(&values[2], 0x10);
    stream->Write(colours[0], 0xC);
    stream->Write(colours[1], 0xC);
}

void TorchObjectSpec::Load(ByteStream* stream) { LoadSpec(stream); }
void TorchObjectSpec::Save(ByteStream* stream) { SaveSpec(stream); }

void TorchObjectSpec::SetLight(std::uint32_t kind, const float* colour, float value) {
    lightKind = kind;
    std::memcpy(colours[0], colour, sizeof(colours[0]));
    values[0] = value;
}

void TorchObjectSpec::SetFlame(const float* colour, float value, const float* newValues) {
    std::memcpy(colours[1], colour, sizeof(colours[1]));
    values[1] = value;
    std::memcpy(&values[2], newValues, 4 * sizeof(float));
}

void TorchObjectSpec::SetName(const char* newName) { name = newName; }

void TorchObjectSpec::ReadNetState(DeltaField* field) {
    std::uint32_t kind;
    field->ReadDelta(&kind, 4);
    lightKind = kind;
    field->ReadDelta(&values[0], 4);
    field->ReadDelta(&values[1], 4);
    field->ReadDelta(&values[2], 0x10);
    field->ReadDelta(colours[0], 0xC);
    field->ReadDelta(colours[1], 0xC);
    name = field->ReadString();
    SetLightSlot(lightKind, colours[0], values[0]);
    SetFlameSlot(colours[1], values[1], &values[2]);
    SetNameSlot(name.c_str());
    ShowEffectsSlot();
    WarmUpParticlesSlot();
}
