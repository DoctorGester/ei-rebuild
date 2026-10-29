// The renderer's helper object (CRenderHelper, model.h: the material presets): its constructor with the nine built-in
// presets' state lists and its destructor, and its preset (the passes it sets in the renderer's render state). See
// docs/notes/renderer.md (sections 5 and 12).
#include "model.h"
#include "render/render_state.h"

#include <cstdint>
#include <iterator>
#include <vector>

namespace {

using State = CRenderPreset::State;

// A texture stage: the colour is `operation` of `argument` and the stage before, the alpha is switched off.
// The new elements are filled from an uninitialised local with only the two flags cleared and then writes the
// fields below, so what a list does not set (stage 0's operations everywhere, the alpha arguments of presets
// 5-7) is stack contents there and zero here; the preset's first pass sets the alpha arguments even so.
State TextureStage(render::TextureOp operation, std::uint8_t argument, bool alphaArguments) {
    State state{};
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

State Blended(State state, render::Blend source, render::Blend dest) {
    state.blend = true;
    state.sourceBlend = source;
    state.destBlend = dest;
    return state;
}

// The state list of built-in preset `index` (below 9). Presets 0 and 8 have no states.
std::vector<State> BuiltInStates(unsigned index) {
    using Op = render::TextureOp;
    using Blend = render::Blend;
    const State unset{};
    std::vector<State> states;
    switch (index) {
    case 1:   // texture x frame
        states.resize(2, unset);
        states[1] = Blended(TextureStage(Op::Modulate, render::kArgTexture, true), Blend::DestColour, Blend::Zero);
        break;
    case 2:   // the texture's alpha x frame
        states.resize(2, unset);
        states[1] = Blended(TextureStage(Op::Modulate, render::kArgTexture | render::kArgAlphaReplicate, true),
                            Blend::Zero, Blend::SourceAlpha);
        break;
    case 3:   // added
        states.resize(2, unset);
        states[1] = Blended(TextureStage(Op::Add, render::kArgTexture, true), Blend::One, Blend::One);
        break;
    case 4:   // blended by the texture's alpha
        states.resize(2, unset);
        states[1] = Blended(TextureStage(Op::BlendTextureAlpha, render::kArgTexture, true), Blend::SourceAlpha,
                            Blend::InvSourceAlpha);
        break;
    case 5:
        states.resize(3, unset);
        states[0] = Blended(unset, Blend::One, Blend::Zero);
        states[1] = Blended(TextureStage(Op::Add, render::kArgTexture, false), Blend::One, Blend::One);
        states[2] = Blended(TextureStage(Op::Modulate2x, render::kArgTexture, false), Blend::DestColour,
                            Blend::SourceColour);
        break;
    case 6:
    case 7:   // bump mapped (7: with luminance), a texture added
        states.resize(3, unset);
        states[1] = TextureStage(index == 6 ? Op::BumpEnvMap : Op::BumpEnvMapLuminance, render::kArgTexture, false);
        states[2] = TextureStage(Op::Add, render::kArgTexture, false);
        break;
    }
    return states;
}

} // namespace

// ---- The helper -------------------------------------------------------------------------------------

// The first pass takes the texture stages of all the states (of one with `singleStage`); each state from there on
// is a pass of its own. (The game took as many stages as the Direct3D device had and checked their operations
// against its capabilities; the presets have at most three states, and the device the game ran on (dgVoodoo's) had
// eight stages and every operation.)
void CRenderHelper::Record(const std::vector<State>& states, unsigned index, bool singleStage) {
    const int count = static_cast<int>(states.size());
    const int stages = singleStage && count > 1 ? 1 : count;
    auto* preset = new CRenderPreset(states, stages);
    preset->AddRef();
    presets_[index] = preset;
}

CRenderHelper::CRenderHelper(std::uint32_t flag) {
    for (unsigned index = 0; index < kPresetCount; ++index)
        Record(BuiltInStates(index), index, static_cast<std::uint8_t>(flag) != 0);
}

CRenderHelper::~CRenderHelper() {
    for (CRenderPreset*& preset : presets_) {
        if (preset) preset->Release();
        preset = nullptr;
    }
}

// ---- The preset -------------------------------------------------------------------------------------

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
    const State& passState = states_[stages_ + pass - 1];
    if (passState.blend) {
        state.sourceBlend = passState.sourceBlend;
        state.destBlend = passState.destBlend;
    }
}
