// The torch's state object, CTorchObjectSpec (server_specific_manager.h): its Create / Destroy,
// Load / Save and setter slots (0052C490-0052C680, A-U6). The network reader 0052C390 stays
// original (it applies the values through slots 13-18, which a client-side spec may override).
#include "server_specific_manager.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <cstring>

namespace {
constexpr float kDefaultValue = 1.0f;
constexpr float kDefaultValue2C = 10.0f;
// 007AF2D0: the default colour both colours start from.
const float* DefaultColour() { return reinterpret_cast<const float*>(0x007AF2D0); }
} // namespace

int CTorchObjectSpec::CreateSpec() {
    unknown_1C = 0;
    unknown_20 = 0;
    name_.Empty();
    values_[1] = kDefaultValue2C;
    values_[0] = kDefaultValue;
    for (int i = 2; i < 6; ++i) values_[i] = kDefaultValue;
    std::memcpy(colours_[0], DefaultColour(), sizeof(colours_[0]));
    std::memcpy(colours_[1], DefaultColour(), sizeof(colours_[1]));
    return 1;
}
EI_REPLACE_METHOD(0x0052C610, CTorchObjectSpec::CreateSpec);

void CTorchObjectSpec::DestroySpec() {
    unknown_1C = 0;
    name_.Empty();
}
EI_REPLACE_METHOD(0x0052C680, CTorchObjectSpec::DestroySpec);

void CTorchObjectSpec::LoadSpec(CStream* stream) {
    stream->Read(&unknown_20, 4);
    StreamReadCStringOriginal(stream, nullptr, &name_);
    stream->Read(&values_[0], 4);
    stream->Read(&values_[1], 4);
    stream->Read(&values_[2], 0x10);
    stream->Read(colours_[0], 0xC);
    stream->Read(colours_[1], 0xC);
}
EI_REPLACE_METHOD(0x0052C490, CTorchObjectSpec::LoadSpec);

void CTorchObjectSpec::SaveSpec(CStream* stream) {
    stream->Write(&unknown_20, 4);
    const char* name = name_;
    stream->Write(name, static_cast<std::uint32_t>(std::strlen(name) + 1));
    std::uint32_t value;
    std::memcpy(&value, &values_[0], 4);   // through a stack copy, as the original
    stream->Write(&value, 4);
    std::memcpy(&value, &values_[1], 4);
    stream->Write(&value, 4);
    stream->Write(&values_[2], 0x10);
    stream->Write(colours_[0], 0xC);
    stream->Write(colours_[1], 0xC);
}
EI_REPLACE_METHOD(0x0052C500, CTorchObjectSpec::SaveSpec);

void CTorchObjectSpec::SetLight(std::uint32_t kind, const float* colour, float value) {
    unknown_20 = kind;
    std::memcpy(colours_[0], colour, sizeof(colours_[0]));
    values_[0] = value;
}
EI_REPLACE_METHOD(0x0052C590, CTorchObjectSpec::SetLight);

void CTorchObjectSpec::SetFlame(const float* colour, float value, const float* values) {
    std::memcpy(colours_[1], colour, sizeof(colours_[1]));
    values_[1] = value;
    std::memcpy(&values_[2], values, 4 * sizeof(float));
}
EI_REPLACE_METHOD(0x0052C5C0, CTorchObjectSpec::SetFlame);

void CTorchObjectSpec::SetName(const char* name) { name_ = name; }
EI_REPLACE_METHOD(0x0052C600, CTorchObjectSpec::SetName);
