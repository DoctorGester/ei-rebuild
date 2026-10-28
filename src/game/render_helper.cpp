// The renderer's helper object (CRenderHelper, model.h: the material presets). The helper (its constructor with the
// nine built-in presets' state lists, the destructor, the presets' making and release, and the slots that read its
// tables), its preset (the passes it sets in the renderer's render state), and the transform pipeline's constructor and
// destructor with its matrix stacks'. See docs/notes/renderer.md (sections 5 and 12).
#include "app.h"
#include "core_refcount.h"
#include "device.h"   // the old device's calls
#include "render/render_state.h"
#include "dx_errors.h"
#include "model.h"
#include "sys_info.h"
#include "texture.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <d3d.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace {

static_assert(sizeof(D3DDEVICEDESC7) == sizeof(CRenderHelper::caps_));
D3DDEVICEDESC7& Caps(CRenderHelper* self) { return *reinterpret_cast<D3DDEVICEDESC7*>(self->caps_); }

// The fatal errors' texts in the game's data.
const char* CantGetCapsText() { return "Can't get device caps\n"; }
const char* AlreadyRegisteredText() { return "Can't register default effect %d\n Already exist"; }

void Retain(CRenderPreset* preset) {
    if (preset) ++preset->refs_;
}
void Release(CRenderPreset* preset) {
    if (preset && --preset->refs_ == 0) delete preset;
}

// a record's destructor: the preset released, the states' storage freed.
void DropPreset(RenderPresetRecord* record) { Release(record->preset); }

// A texture stage: the colour is `operation` of `argument` and the stage before, the alpha is switched off.
// The new elements are filled from an uninitialised local with only the two flags cleared and then writes the
// fields below, so what a list does not set (stage 0's operations everywhere, the alpha arguments of presets
// 5-7) is stack contents there and zero here; the preset's first pass sets the alpha arguments even so.
RenderPresetRecord::State TextureStage(render::TextureOp operation, std::uint8_t argument, bool alphaArguments) {
    RenderPresetRecord::State state{};
    state.enabled = true;
    state.combiner.colourOp = operation;
    state.combiner.colourArg1 = argument;
    state.combiner.colourArg2 = render::kArgCurrent;
    state.combiner.alphaOp = render::TextureOp::Disable;
    if (alphaArguments) {
        state.combiner.alphaArg1 = render::kArgTexture;
        state.combiner.alphaArg2 = render::kArgCurrent;
    }
    return state;
}

RenderPresetRecord::State Blended(RenderPresetRecord::State state, render::Blend source, render::Blend dest) {
    state.blend = true;
    state.sourceBlend = source;
    state.destBlend = dest;
    return state;
}

const char* TransformLibraryName() { return "3DfpFPU.dll"; }
const char* TransformFunctionName() { return "GetTLPipeline"; }
// The debug messages ("FPU Transform release library loaded." and "Acquiring geometry processor
// from transform library.") and the errors' texts ("Can't load 3D transform library." and "Loaded
// 3D transform library have no function "GetTLPipeline"."), each with a newline.
const char* LibraryLoadedText() { return "FPU Transform release library loaded.\n"; }
const char* AcquiringText() { return "Acquiring geometry processor from transform library.\n"; }
const char* NoLibraryText() { return "Can't load 3D transform library.\n"; }
const char* NoFunctionText() { return "Loaded 3D transform library have no function \"GetTLPipeline\".\n"; }

} // namespace

// ---- The built-in presets' states -------------------------------------------------------------------

// The state list of built-in preset `index` into `states`
// (emptied first); false beyond the nine. Presets 0 and 8 have no states.
bool CRenderHelper::RenderHelperBuiltInStateList(std::vector<RenderPresetRecord::State>* states, unsigned index) {
    states->clear();
    if (index >= CRenderHelper::kPresetCount) return false;
    using Op = render::TextureOp;
    using Blend = render::Blend;
    const RenderPresetRecord::State unset{};
    switch (index) {
    case 1:   // texture x frame
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(Op::Modulate, render::kArgTexture, true), Blend::DestColour, Blend::Zero);
        break;
    case 2:   // the texture's alpha x frame
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(Op::Modulate, render::kArgTexture | render::kArgAlphaReplicate, true),
                               Blend::Zero, Blend::SourceAlpha);
        break;
    case 3:   // added
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(Op::Add, render::kArgTexture, true), Blend::One, Blend::One);
        break;
    case 4:   // blended by the texture's alpha
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(Op::BlendTextureAlpha, render::kArgTexture, true), Blend::SourceAlpha,
                               Blend::InvSourceAlpha);
        break;
    case 5:
        states->resize(3, unset);
        (*states)[0] = Blended(unset, Blend::One, Blend::Zero);
        (*states)[1] = Blended(TextureStage(Op::Add, render::kArgTexture, false), Blend::One, Blend::One);
        (*states)[2] = Blended(TextureStage(Op::Modulate2x, render::kArgTexture, false), Blend::DestColour,
                               Blend::SourceColour);
        break;
    case 6:
    case 7:   // bump mapped (7: with luminance), a texture added
        states->resize(3, unset);
        (*states)[1] = TextureStage(index == 6 ? Op::BumpEnvMap : Op::BumpEnvMapLuminance, render::kArgTexture, false);
        (*states)[2] = TextureStage(Op::Add, render::kArgTexture, false);
        break;
    }
    return true;
}

// ---- Presets -----------------------------------------------------------------------------------------

// The first pass takes the texture stages of the first `stages` states (as many as the device has, or one); each
// state from there on is a pass of its own. The check of a stage's operations ands the operation's number with the
// device's capability bits; a stage that fails it ends the first pass's stages there. Returns the new preset.
CRenderPreset* CRenderHelper::Record(const std::vector<RenderPresetRecord::State>* states, int index, bool singleStage) {
    const int count = static_cast<int>(states->size());
    int stages;
    if (singleStage) {
        stages = count > 1 ? 1 : count;
    } else {
        const int deviceStages = Caps(this).wMaxSimultaneousTextures;
        stages = deviceStages < count ? deviceStages : count;
    }
    const std::uint32_t operations = Caps(this).dwTextureOpCaps;
    for (int i = 0; i < stages; ++i) {
        const RenderPresetRecord::State& state = (*states)[i];
        if (!state.enabled) continue;
        if ((static_cast<std::uint32_t>(state.combiner.colourOp) & operations) == 0 ||
            (static_cast<std::uint32_t>(state.combiner.alphaOp) & operations) == 0) {
            stages = i;
            break;
        }
    }

    auto* preset = new CRenderPreset(*states, stages, index);

    if (index < static_cast<int>(kPresetCount)) {   // signed compare
        if (presets_[index] == nullptr) presets_[index] = preset;
        else game::FatalError(AlreadyRegisteredText(), index);
    }

    // records_[preset] = {a copy of the states, the preset}.
    RenderPresetRecord& record = records_[preset];
    record.states = *states;
    CRenderPreset* previous = record.preset;
    record.preset = preset;
    Retain(preset);
    Release(previous);

    return preset;
}

// The preset's place among the built-in presets cleared, and its record erased. The record holds the preset, so
// this usually deletes it.
void CRenderHelper::Forget(CRenderPreset* preset) {
    if (preset->Index() < kPresetCount) presets_[preset->Index()] = nullptr;

    if (auto node = records_.find(preset); node != records_.end()) {
        DropPreset(&node->second);
        records_.erase(node);
    }
}

// ---- The helper's lifetime ----------------------------------------------------------------------------

// Takes the device and its capabilities, then records the nine
// built-in presets (`flag`'s low byte: one texture stage each).
CRenderHelper::CRenderHelper(IDirect3DDevice7* device, std::uint32_t flag) : device_(nullptr) {
    device_ = device;
    if (device) device::AddRef(device);

    std::memset(caps_, 0, sizeof(caps_));
    const HRESULT result = device::GetCaps(device_, &Caps(this));
    if (result < 0) {
        app::FatalErrorCode(result, CantGetCapsText());
        std::memset(caps_, 0, sizeof(caps_));
    }

    presets_.resize(CRenderHelper::kPresetCount);
    std::vector<RenderPresetRecord::State> states;
    for (unsigned index = 0; index < CRenderHelper::kPresetCount; ++index) {
        if (RenderHelperBuiltInStateList(&states, index)) {
            Record(&states, static_cast<int>(index), static_cast<std::uint8_t>(flag) != 0);
        }
    }
}

// The destructor body. Every record's preset is forgotten. (The game's walk stood on the erased record and read
// its freed node, so it could stray and leave records behind; here every record goes.)
CRenderHelper::~CRenderHelper() {
    std::vector<CRenderPreset*> presets;
    for (const auto& [preset, record] : records_) presets.push_back(preset);
    for (CRenderPreset* preset : presets)
        if (auto found = records_.find(preset); found != records_.end()) Forget(found->second.preset);
    for (auto& [preset, record] : records_) DropPreset(&record);
    records_.clear();
    if (device_) device::Release(device_);
}


// ---- The helper's table slots -----------------------------------------------------------------------

void CRenderHelper::ReleasePreset(CRenderPreset* preset) {
    if (auto node = records_.find(preset); node != records_.end()) Forget(node->second.preset);
}

void* CRenderHelper::PresetBlock(CRenderPreset* preset) {
    auto node = records_.find(preset);
    return node != records_.end() ? node->second.preset : nullptr;
}

CRenderPreset* CRenderHelper::Preset(unsigned index) {
    return index < CRenderHelper::kPresetCount ? presets_[index] : nullptr;
}

// The result is a byte (a bool).
bool CRenderHelper::PresetStates(CRenderPreset* preset, std::vector<RenderPresetRecord::State>* out) {
    auto node = records_.find(preset);
    if (node == records_.end()) return false;
    *out = node->second.states;
    return true;
}

// The result is a byte (a bool).
bool CRenderHelper::BuiltInStates(unsigned index, std::vector<RenderPresetRecord::State>* out) {
    if (index >= CRenderHelper::kPresetCount || !presets_[index]) return false;
    return PresetStates(presets_[index], out);
}

// ---- The preset -------------------------------------------------------------------------------------

CRenderPreset::CRenderPreset(const std::vector<RenderPresetRecord::State>& states, int stages, int index)
    : states_(states), stages_(stages), index_(index) {}

CRenderPreset::~CRenderPreset() {}

// The renderer draws with two texture stages: what the first pass sets on later ones is not kept.
void CRenderPreset::ApplyPass(int pass) const {
    render::RenderState& state = render::g_renderState;
    constexpr int kStages = static_cast<int>(std::size(state.stages));
    if (pass == 0) {
        for (int i = 0; i < stages_ && i < kStages; ++i)
            if (states_[i].enabled) state.stages[i].combiner = states_[i].combiner;
        if (stages_ < kStages) {
            state.stages[stages_].combiner.colourOp = render::TextureOp::Disable;
            state.stages[stages_].combiner.alphaOp = render::TextureOp::Disable;
        }
        return;
    }
    const RenderPresetRecord::State& passState = states_[stages_ + pass - 1];
    if (passState.blend) {
        state.sourceBlend = passState.sourceBlend;
        state.destBlend = passState.destBlend;
    }
}


// The helper's and the preset's abstract bases have no fields; their deleting destructors,
// which no object reaches, are not modelled.

// ---- The transform pipeline (CTransformPipeline) -------------------------------------------------------------

// The processor's CPUID function 1 into the globals (as the renderer does again). A throw unwinds the
// folder's string, the stacks (from the last) and the base, and `new` frees the memory.
CTransformPipeline::CTransformPipeline(const char* folder) {
    std::string path(folder);
    ReadProcessorInfo();
    library_ = gameapi::LoadLibraryA((path + TransformLibraryName()).data());
    game::DebugLog(LibraryLoadedText());
    if (!library_) ThrowGameError(NoLibraryText());

    using GetTLPipeline = ITransformPipeline*(*)();
    auto getPipeline = reinterpret_cast<GetTLPipeline>(
        gameapi::GetProcAddress(static_cast<HMODULE>(library_), TransformFunctionName()));
    if (!getPipeline) {
        gameapi::FreeLibrary(static_cast<HMODULE>(library_));
        library_ = nullptr;
        ThrowGameError(NoFunctionText());
    }
    game::DebugLog(AcquiringText());
    transform_ = getPipeline();
}

// The stacks' blocks go after the body (members), from the last to the first.
CTransformPipeline::~CTransformPipeline() {
    void* library = library_;
    transform_ = nullptr;
    if (library) gameapi::FreeLibrary(static_cast<HMODULE>(library));
    library_ = nullptr;
}
