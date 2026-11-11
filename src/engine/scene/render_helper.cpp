// The renderer's helper object (RenderHelper, render_helper.h: the material presets): its constructor with the nine built-in
// presets' state lists and its destructor, and its preset (the passes it sets in the renderer's render state). See
// docs/notes/renderer.md (sections 5 and 12).
#include "engine/scene/render_helper.h"
#include "render/render_state.h"

#include <cstdint>
#include <iterator>
#include <vector>

namespace {

using State = RenderPreset::State;

// A texture stage: the colour is `operation` of `argument` and the stage before, the alpha is switched off.
// The new elements are filled from an uninitialised local with only the two flags cleared and then writes the
// fields below, so what a list does not set (stage 0's operations everywhere, the alpha arguments of presets
// 5-7) is stack contents there and zero here; the preset's first pass sets the alpha arguments even so.
State TextureStage(render::TextureOp operation, render::TextureArg argument, bool alphaArguments) {
    State state{};
    state.enabled = true;
    state.combiner.colourOp = operation;
    state.combiner.colourArg1 = argument;
    state.combiner.colourArg2 = render::TextureArg::Current;
    state.combiner.alphaOp = render::TextureOp::Disable;
    if (alphaArguments) {
        state.combiner.alphaArg1 = render::TextureArg::Texture;
        state.combiner.alphaArg2 = render::TextureArg::Current;
    }
    return state;
}

// The state list of built-in preset `index` (below 9). Presets 0 and 8 have no states.
std::vector<State> BuiltInStates(unsigned index) {
    using Op = render::TextureOp;
    const State unset{};
    std::vector<State> states;
    switch (index) {
    case 1:   // texture x frame
        states.resize(2, unset);
        states[1] = TextureStage(Op::Modulate, render::TextureArg::Texture, true);
        break;
    case 2:   // the texture's alpha x frame
        states.resize(2, unset);
        states[1] = TextureStage(Op::Modulate, render::TextureArg::Texture | render::TextureArg::AlphaReplicate, true);
        break;
    case 3:   // added
        states.resize(2, unset);
        states[1] = TextureStage(Op::Add, render::TextureArg::Texture, true);
        break;
    case 4:   // blended by the texture's alpha
        states.resize(2, unset);
        states[1] = TextureStage(Op::BlendTextureAlpha, render::TextureArg::Texture, true);
        break;
    case 5:
        states.resize(3, unset);
        states[1] = TextureStage(Op::Add, render::TextureArg::Texture, false);
        states[2] = TextureStage(Op::Modulate2x, render::TextureArg::Texture, false);
        break;
    case 6:
    case 7:   // bump mapped (7: with luminance), a texture added
        states.resize(3, unset);
        states[1] = TextureStage(index == 6 ? Op::BumpEnvMap : Op::BumpEnvMapLuminance, render::TextureArg::Texture, false);
        states[2] = TextureStage(Op::Add, render::TextureArg::Texture, false);
        break;
    }
    return states;
}

} // namespace

// ---- The helper -------------------------------------------------------------------------------------

// The one pass takes the texture stages of all the states. (The game took as many stages as the Direct3D device had and checked their operations
// against its capabilities; the presets have at most three states, and the device the game ran on (dgVoodoo's) had
// eight stages and every operation.)
void RenderHelper::Record(const std::vector<State>& states, unsigned index) {
    auto* preset = new RenderPreset(states);
    preset->AddRef();
    presets[index] = preset;
}

RenderHelper::RenderHelper() {
    for (unsigned index = 0; index < kPresetCount; ++index) Record(BuiltInStates(index), index);
}

RenderHelper::~RenderHelper() {
    for (RenderPreset*& preset : presets) {
        if (preset) preset->Release();
        preset = nullptr;
    }
}

// ---- The preset -------------------------------------------------------------------------------------

// The renderer draws with two texture stages; the stage after the preset's is turned off.
void RenderPreset::Apply() const {
    render::RenderState& state = render::g_renderState;
    constexpr int kStages = static_cast<int>(std::size(state.stages));
    const int stages = StageCount();
    for (int i = 0; i < stages && i < kStages; ++i)
        if (states[i].enabled) state.stages[i].combiner = states[i].combiner;
    if (stages < kStages) {
        state.stages[stages].combiner.colourOp = render::TextureOp::Disable;
        state.stages[stages].combiner.alphaOp = render::TextureOp::Disable;
    }
}
