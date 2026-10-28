// The renderer's helper object (CRenderHelper, model.h: the Direct3D state blocks behind the material presets).
// The helper (its constructor with the nine built-in presets' state lists, the destructor,
// the state blocks' recording and release, and the slots that read its tables), its preset's constructor and
// destructor, the Direct3D driver list's lookup by index, and the transform pipeline's constructor and destructor
// with its matrix stacks'. See docs/notes/renderer.md (section 12).
#include "app.h"
#include "core_refcount.h"
#include "device.h"   // the old device's calls
#include "dx_errors.h"
#include "model.h"
#include "sys_info.h"
#include "texture.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "game_data.h"

#include <d3d.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using State = RenderPresetRecord::State;
using StateVector = std::vector<State>;

static_assert(sizeof(D3DDEVICEDESC7) == sizeof(CRenderHelper::caps_));
D3DDEVICEDESC7& Caps(CRenderHelper* self) { return *reinterpret_cast<D3DDEVICEDESC7*>(self->caps_); }

// The fatal errors' texts in the game's data.
const char* CantGetCapsText() { return "Can't get device caps\n"; }
const char* AlreadyRegisteredText() { return "Can't register default effect %d\n Already exist"; }
const char* CantEndBlockText() { return "Can't end state block\n"; }
const char* CantBeginBlockText() { return "Can't begin state block\n"; }
const char* CantDeleteBlockText() { return "Can't delete state block %d\n"; }

void Retain(CRenderPreset* preset) {
    if (preset) ++preset->refs_;
}
void Release(CRenderPreset* preset) {
    if (preset && --preset->refs_ == 0) delete preset;
}

// a record's destructor: the preset released, the states' storage freed.
void DropPreset(RenderPresetRecord* record) { Release(record->preset); }

// What a failed Direct3D call leaves as its result: the fatal error's (0 when it comes back).
CRenderPreset* Failed(int errorResult) { return reinterpret_cast<CRenderPreset*>(static_cast<std::uintptr_t>(errorResult)); }



} // namespace

// ---- The built-in presets' states -------------------------------------------------------------------

namespace {

// A texture stage: the colour is `operation` of `argument` and the stage before, the alpha is switched off.
// The new elements are filled from an uninitialised local with only the two flags cleared and then writes the
// fields below, so what a list does not set (stage 0's operations everywhere, the alpha arguments of presets
// 5-7) is stack contents there and zero here; the helper's slot 1 passes the alpha arguments on to
// Direct3D even so.
State TextureStage(D3DTEXTUREOP operation, std::uint32_t argument, bool alphaArguments) {
    State state{};
    state.enabled = true;
    state.colourOp = operation;
    state.colourArg1 = argument;
    state.colourArg2 = D3DTA_CURRENT;
    state.alphaOp = D3DTOP_DISABLE;
    if (alphaArguments) {
        state.alphaArg1 = D3DTA_TEXTURE;
        state.alphaArg2 = D3DTA_CURRENT;
    }
    return state;
}

State Blended(State state, D3DBLEND source, D3DBLEND dest) {
    state.blend = true;
    state.sourceBlend = source;
    state.destBlend = dest;
    return state;
}

} // namespace

// The state list of built-in preset `index` into `states`
// (emptied first); false beyond the nine. Presets 0 and 8 have no states.
bool CRenderHelper::RenderHelperBuiltInStateList(StateVector* states, unsigned index) {
    states->clear();
    if (index >= CRenderHelper::kPresetCount) return false;
    const State unset{};
    switch (index) {
    case 1:   // texture x frame
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_MODULATE, D3DTA_TEXTURE, true), D3DBLEND_DESTCOLOR, D3DBLEND_ZERO);
        break;
    case 2:   // the texture's alpha x frame
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_MODULATE, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE, true),
                               D3DBLEND_ZERO, D3DBLEND_SRCALPHA);
        break;
    case 3:   // added
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, true), D3DBLEND_ONE, D3DBLEND_ONE);
        break;
    case 4:   // blended by the texture's alpha
        states->resize(2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_BLENDTEXTUREALPHA, D3DTA_TEXTURE, true), D3DBLEND_SRCALPHA,
                               D3DBLEND_INVSRCALPHA);
        break;
    case 5:
        states->resize(3, unset);
        (*states)[0] = Blended(unset, D3DBLEND_ONE, D3DBLEND_ZERO);
        (*states)[1] = Blended(TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, false), D3DBLEND_ONE, D3DBLEND_ONE);
        (*states)[2] = Blended(TextureStage(D3DTOP_MODULATE2X, D3DTA_TEXTURE, false), D3DBLEND_DESTCOLOR,
                               D3DBLEND_SRCCOLOR);
        break;
    case 6:
    case 7:   // bump mapped (7: with luminance), a texture added
        states->resize(3, unset);
        (*states)[1] = TextureStage(index == 6 ? D3DTOP_BUMPENVMAP : D3DTOP_BUMPENVMAPLUMINANCE, D3DTA_TEXTURE, false);
        (*states)[2] = TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, false);
        break;
    }
    return true;
}

// ---- State blocks -------------------------------------------------------------------------------------

// The first block sets the texture stages of the first `stages` states (as many as the device has, or one)
// and switches the next stage off; each state from there on gets a block of its own with its blend factors
// (an empty block without them). The check of a stage's operations ands the operation's number with the
// device's capability bits. A stage that fails it ends the first block there. Returns the new preset.
CRenderPreset* CRenderHelper::Record(const StateVector* states, int index, bool singleStage) {
    const int count = static_cast<int>(states->size());
    int stages;
    if (singleStage) {
        stages = count > 1 ? 1 : count;
    } else {
        const int deviceStages = Caps(this).wMaxSimultaneousTextures;
        stages = deviceStages < count ? deviceStages : count;
    }
    std::vector<std::uint32_t> blocks;
    blocks.resize(static_cast<std::size_t>(count - stages + 1), std::uint32_t{0});

    HRESULT result = device::BeginStateBlock(device_);
    if (result < 0) {
        const int error = app::FatalErrorCode(result, CantBeginBlockText());
        return Failed(error);
    }
    for (int i = 0; i < stages; ++i) {
        const State& state = (*states)[i];
        if (!state.enabled) continue;
        const std::uint32_t operations = Caps(this).dwTextureOpCaps;
        if ((state.colourOp & operations) == 0 || (state.alphaOp & operations) == 0) {
            stages = i;
            blocks.resize(states->size() - i + 1, std::uint32_t{0});
            break;
        }
        device::SetTextureStageState(device_, i, D3DTSS_COLOROP, state.colourOp);
        device::SetTextureStageState(device_, i, D3DTSS_COLORARG1, state.colourArg1);
        device::SetTextureStageState(device_, i, D3DTSS_COLORARG2, state.colourArg2);
        device::SetTextureStageState(device_, i, D3DTSS_ALPHAOP, state.alphaOp);
        device::SetTextureStageState(device_, i, D3DTSS_ALPHAARG1, state.alphaArg1);
        device::SetTextureStageState(device_, i, D3DTSS_ALPHAARG2, state.alphaArg2);
    }
    device::SetTextureStageState(device_, stages, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device::SetTextureStageState(device_, stages, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    DWORD block = 0;
    result = device::EndStateBlock(device_, &block);
    if (result < 0) {
        const int error = app::FatalErrorCode(result, CantEndBlockText());
        return Failed(error);
    }
    blocks[0] = block;

    for (std::size_t i = static_cast<std::size_t>(stages); i < states->size(); ++i) {
        result = device::BeginStateBlock(device_);
        if (result < 0) {
            const int error = app::FatalErrorCode(result, CantBeginBlockText());
            return Failed(error);
        }
        const State& state = (*states)[i];
        if (state.blend) {
            device::SetRenderState(device_, D3DRENDERSTATE_SRCBLEND, state.sourceBlend);
            device::SetRenderState(device_, D3DRENDERSTATE_DESTBLEND, state.destBlend);
        }
        result = device::EndStateBlock(device_, &block);
        if (result < 0) {
            const int error = app::FatalErrorCode(result, CantEndBlockText());
            return Failed(error);
        }
        blocks[i - stages + 1] = block;
    }

    auto* preset = new CRenderPreset(blocks, stages, index);

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

// The preset's state blocks deleted, its place among the built-in presets cleared, and its record erased. The
// record holds the preset, so this usually deletes it.
void CRenderHelper::ReleaseBlock(CRenderPreset* preset) {
    for (int i = 0; i < static_cast<int>(preset->blocks_.size()); ++i) {
        const std::uint32_t block = preset->blocks_[i];
        const HRESULT result = device::DeleteStateBlock(device_, block);
        if (result < 0) app::FatalErrorCode(result, CantDeleteBlockText(), block);
    }
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
    StateVector states;
    for (unsigned index = 0; index < CRenderHelper::kPresetCount; ++index) {
        if (RenderHelperBuiltInStateList(&states, index)) {
            Record(&states, static_cast<int>(index), static_cast<std::uint8_t>(flag) != 0);
        }
    }
}

// The destructor body. Every record's preset goes through slot 3 (its state blocks deleted, the record
// erased). (The game's walk stood on the erased record and read its freed node, so it could stray and leave
// records behind with their state blocks; here every record goes.)
CRenderHelper::~CRenderHelper() {
    std::vector<CRenderPreset*> presets;
    for (const auto& [preset, record] : records_) presets.push_back(preset);
    for (CRenderPreset* preset : presets)
        if (auto found = records_.find(preset); found != records_.end()) ReleaseBlock(found->second.preset);
    for (auto& [preset, record] : records_) DropPreset(&record);
    records_.clear();
    if (device_) device::Release(device_);
}

void* CRenderHelper::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CRenderHelper::operator delete(void* block) { gamecrt::operator_delete(block); }

// ---- The helper's table slots -----------------------------------------------------------------------

void CRenderHelper::ReleasePreset(CRenderPreset* preset) {
    if (auto node = records_.find(preset); node != records_.end()) ReleaseBlock(node->second.preset);
}

void* CRenderHelper::PresetBlock(CRenderPreset* preset) {
    auto node = records_.find(preset);
    return node != records_.end() ? node->second.preset : nullptr;
}

CRenderPreset* CRenderHelper::Preset(unsigned index) {
    return index < CRenderHelper::kPresetCount ? presets_[index] : nullptr;
}

// The result is a byte (a bool).
bool CRenderHelper::PresetStates(CRenderPreset* preset, StateVector* out) {
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

CRenderPreset::CRenderPreset(const std::vector<std::uint32_t>& blocks, int stages, int index)
    : blocks_(blocks), stages_(stages), index_(index) {}

CRenderPreset::~CRenderPreset() {  }

void* CRenderPreset::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CRenderPreset::operator delete(void* block) { gamecrt::operator_delete(block); }

// The helper's and the preset's abstract bases have no fields; their deleting destructors,
// which no object reaches, are not modelled.

// ---- The transform pipeline (CTransformPipeline) -------------------------------------------------------------

namespace {

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

// The processor's CPUID function 1 into the globals (as the renderer does again). A throw unwinds the
// folder's string, the stacks (from the last) and the base, and `new` frees the memory.
CTransformPipeline::CTransformPipeline(const char* folder) {
    std::string path(folder);
    ReadProcessorInfo();
    library_ = gameapi::LoadLibraryA((path + TransformLibraryName()).data());
    game::DebugLog(LibraryLoadedText());
    if (!library_) ThrowGameError(NoLibraryText());

    using GetTLPipeline = ITransformPipeline*(__cdecl*)();
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
