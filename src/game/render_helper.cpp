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

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <d3d.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using State = RenderPresetRecord::State;
using StateVector = stlp::vector<State>;

static_assert(sizeof(D3DDEVICEDESC7) == sizeof(CRenderHelper::caps_));
D3DDEVICEDESC7& Caps(CRenderHelper* self) { return *reinterpret_cast<D3DDEVICEDESC7*>(self->caps_); }

// The fatal errors' texts in the game's data.
const char* CantGetCapsText() { return "Can't get device caps\n"; }
const char* AlreadyRegisteredText() { return "Can't register default effect %d\n Already exist"; }
const char* CantEndBlockText() { return "Can't end state block\n"; }
const char* CantBeginBlockText() { return "Can't begin state block\n"; }
const char* CantDeleteBlockText() { return "Can't delete state block %d\n"; }

// vector<T>::resize(count, value) as STLport writes it for plain elements (and
// their fill inserts): shorter keeps the storage; longer fills at the end, in a new block of
// size + max(size, added) elements when the old one has no room.
template <typename T>
void Resize(stlp::vector<T>* v, std::size_t count, const T& value) {
    const std::size_t size = v->size();
    if (count < size) {
        v->Finish() = v->Start() + count;
        return;
    }
    const std::size_t added = count - size;
    if (added == 0) return;
    if (static_cast<std::size_t>(v->EndOfStorage() - v->Finish()) < added) {
        const std::size_t capacity = size + (size > added ? size : added);
        T* fresh = stlp::allocate_as<T>(capacity);
        if (size != 0) std::memcpy(fresh, v->Start(), size * sizeof(T));
        v->FreeStorage();
        v->Start() = fresh;
        v->Finish() = fresh + size;
        v->EndOfStorage() = fresh + capacity;
    }
    for (std::size_t i = 0; i < added; ++i) v->Finish()[i] = value;
    v->Finish() += added;
}

// vector<T>(source) for plain elements: a block of exactly the source's length.
template <typename T>
void CopyConstruct(stlp::vector<T>* v, const stlp::vector<T>& source) {
    const std::size_t count = source.size();
    T* block = count != 0 ? stlp::allocate_as<T>(count) : nullptr;
    if (count != 0) std::memcpy(block, source.begin(), count * sizeof(T));
    v->Start() = block;
    v->Finish() = v->EndOfStorage() = block + count;
}

void Retain(CRenderPreset* preset) {
    if (preset) ++preset->refs_;
}
void Release(CRenderPreset* preset) {
    if (preset && --preset->refs_ == 0) delete preset;
}

// a record's destructor: the preset released, the states' storage freed.
void DestroyRecord(RenderPresetRecord* record) {
    Release(record->preset);
    record->states.FreeStorage();
}

// What a failed Direct3D call leaves as its result: the fatal error's (0 when it comes back).
CRenderPreset* Failed(int errorResult) { return reinterpret_cast<CRenderPreset*>(static_cast<std::uintptr_t>(errorResult)); }

// vector<State>::operator= as STLport writes it (inlines it; the states are plain
// records): a source longer than the capacity gets a new block of exactly its length (the old one
// freed after the copy), otherwise the elements are copied over the old ones.
void AssignStates(StateVector* out, const StateVector& source) {
    using State = RenderPresetRecord::State;
    if (out == &source) return;
    const std::size_t count = source.size();
    const std::size_t capacity = static_cast<std::size_t>(out->EndOfStorage() - out->Start());
    if (count > capacity) {
        auto* fresh = stlp::allocate_as<State>(count);
        std::memcpy(fresh, source.begin(), count * sizeof(State));
        if (out->Start()) stlp::deallocate(out->Start(), capacity * sizeof(State));
        out->Start() = fresh;
        out->EndOfStorage() = fresh + count;
    } else if (count != 0) {
        std::memmove(out->Start(), source.begin(), count * sizeof(State));
    }
    out->Finish() = out->Start() + count;
}

constexpr int kMatrixStackDepth = 16;   // matrices a stack has room for


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
    states->Finish() = states->Start();
    if (index >= CRenderHelper::kPresetCount) return false;
    const State unset{};
    switch (index) {
    case 1:   // texture x frame
        Resize(states, 2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_MODULATE, D3DTA_TEXTURE, true), D3DBLEND_DESTCOLOR, D3DBLEND_ZERO);
        break;
    case 2:   // the texture's alpha x frame
        Resize(states, 2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_MODULATE, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE, true),
                               D3DBLEND_ZERO, D3DBLEND_SRCALPHA);
        break;
    case 3:   // added
        Resize(states, 2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, true), D3DBLEND_ONE, D3DBLEND_ONE);
        break;
    case 4:   // blended by the texture's alpha
        Resize(states, 2, unset);
        (*states)[1] = Blended(TextureStage(D3DTOP_BLENDTEXTUREALPHA, D3DTA_TEXTURE, true), D3DBLEND_SRCALPHA,
                               D3DBLEND_INVSRCALPHA);
        break;
    case 5:
        Resize(states, 3, unset);
        (*states)[0] = Blended(unset, D3DBLEND_ONE, D3DBLEND_ZERO);
        (*states)[1] = Blended(TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, false), D3DBLEND_ONE, D3DBLEND_ONE);
        (*states)[2] = Blended(TextureStage(D3DTOP_MODULATE2X, D3DTA_TEXTURE, false), D3DBLEND_DESTCOLOR,
                               D3DBLEND_SRCCOLOR);
        break;
    case 6:
    case 7:   // bump mapped (7: with luminance), a texture added
        Resize(states, 3, unset);
        (*states)[1] = TextureStage(index == 6 ? D3DTOP_BUMPENVMAP : D3DTOP_BUMPENVMAPLUMINANCE, D3DTA_TEXTURE, false);
        (*states)[2] = TextureStage(D3DTOP_ADD, D3DTA_TEXTURE, false);
        break;
    }
    return true;
}
EI_REWRITTEN(0x004FCE90, CRenderHelper::RenderHelperBuiltInStateList);

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
    stlp::vector<std::uint32_t> blocks;
    Resize(&blocks, static_cast<std::size_t>(count - stages + 1), std::uint32_t{0});

    HRESULT result = device::BeginStateBlock(device_);
    if (result < 0) {
        const int error = app::FatalErrorCode(result, CantBeginBlockText());
        blocks.FreeStorage();
        return Failed(error);
    }
    for (int i = 0; i < stages; ++i) {
        const State& state = (*states)[i];
        if (!state.enabled) continue;
        const std::uint32_t operations = Caps(this).dwTextureOpCaps;
        if ((state.colourOp & operations) == 0 || (state.alphaOp & operations) == 0) {
            stages = i;
            Resize(&blocks, states->size() - i + 1, std::uint32_t{0});
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
        blocks.FreeStorage();
        return Failed(error);
    }
    blocks[0] = block;

    for (std::size_t i = static_cast<std::size_t>(stages); i < states->size(); ++i) {
        result = device::BeginStateBlock(device_);
        if (result < 0) {
            const int error = app::FatalErrorCode(result, CantBeginBlockText());
            blocks.FreeStorage();
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
            blocks.FreeStorage();
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
    StateVector copy;
    CopyConstruct(&copy, *states);
    RenderPresetRecord& record = records_[preset];
    AssignStates(&record.states, copy);
    CRenderPreset* previous = record.preset;
    record.preset = preset;
    Retain(preset);
    Release(previous);
    copy.FreeStorage();

    blocks.FreeStorage();
    return preset;
}

EI_REWRITTEN(0x004FD2A0, CRenderHelper::Record);

// The preset's state blocks deleted, its place among the built-in presets
// cleared, and its record erased (hashtable::erase(key) as STLport writes it: the bucket's later
// nodes first, then its head). The record holds the preset, so this usually deletes it.
void CRenderHelper::ReleaseBlock(CRenderPreset* preset) {
    for (int i = 0; i < static_cast<int>(preset->blocks_.size()); ++i) {
        const std::uint32_t block = preset->blocks_[i];
        const HRESULT result = device::DeleteStateBlock(device_, block);
        if (result < 0) app::FatalErrorCode(result, CantDeleteBlockText(), block);
    }
    if (preset->Index() < kPresetCount) presets_[preset->Index()] = nullptr;

    using Node = decltype(records_)::Node;
    Node* head = records_.Bucket(stlp::PointerHash()(preset) % records_.BucketCount());
    if (!head) return;
    for (Node* node = head->next; node;) {
        Node* next = node->next;
        if (node->key == preset) {
            DestroyRecord(&node->value);
            records_.Erase(node);
        }
        node = next;
    }
    if (head->key == preset) {
        DestroyRecord(&head->value);
        records_.Erase(head);
    }
}

EI_REWRITTEN(0x004FD9D0, CRenderHelper::ReleaseBlock);

// ---- The helper's lifetime ----------------------------------------------------------------------------

// Takes the device and its capabilities, then records the nine
// built-in presets (`flag`'s low byte: one texture stage each).
CRenderHelper::CRenderHelper(IDirect3DDevice7* device, std::uint32_t flag) : device_(nullptr) {
    records_.InitBuckets(100);
    presets_.Start() = presets_.Finish() = presets_.EndOfStorage() = nullptr;

    device_ = device;
    if (device) device::AddRef(device);

    std::memset(caps_, 0, sizeof(caps_));
    const HRESULT result = device::GetCaps(device_, &Caps(this));
    if (result < 0) {
        app::FatalErrorCode(result, CantGetCapsText());
        std::memset(caps_, 0, sizeof(caps_));
    }

    Resize(&presets_, CRenderHelper::kPresetCount, static_cast<CRenderPreset*>(nullptr));
    StateVector states;
    for (unsigned index = 0; index < CRenderHelper::kPresetCount; ++index) {
        if (RenderHelperBuiltInStateList(&states, index)) {
            Record(&states, static_cast<int>(index), static_cast<std::uint8_t>(flag) != 0);
        }
    }
    states.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x004FCA60, CRenderHelper::CRenderHelper);

// The destructor body. Every record's preset goes through slot 3, which erases the record
// the walk stands on: the step to the next record then reads the freed node (its
// link is the node allocator's free list by then), so the walk can stray and leave records behind.
// Those are destroyed with the table (their presets deleted, but not their state blocks).
CRenderHelper::~CRenderHelper() {
    using Node = decltype(records_)::Node;
    for (Node* node = records_.First(); node; node = records_.After(node)) {
        if (Node* found = records_.Find(node->key)) ReleaseBlock(found->value.preset);
    }
    presets_.FreeStorage();
    records_.ClearWith([](Node* node) {
        DestroyRecord(&node->value);
        stlp::deallocate(node, sizeof(Node));
    });
    records_.Destroy();
    if (device_) device::Release(device_);
}
EI_REWRITTEN_SPECIAL(0x004FCD50, CRenderHelper::~CRenderHelper);
EI_REWRITTEN_SPECIAL(0x004FCC20, CRenderHelper::~CRenderHelper);

void* CRenderHelper::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CRenderHelper::operator delete(void* block) { gamecrt::operator_delete(block); }

// ---- The helper's table slots -----------------------------------------------------------------------

void CRenderHelper::ReleasePreset(CRenderPreset* preset) {
    if (auto* node = records_.Find(preset)) ReleaseBlock(node->value.preset);
}
EI_REWRITTEN(0x004FDB00, CRenderHelper::ReleasePreset);

void* CRenderHelper::PresetBlock(CRenderPreset* preset) {
    auto* node = records_.Find(preset);
    return node ? node->value.preset : nullptr;
}
EI_REWRITTEN(0x004FDB50, CRenderHelper::PresetBlock);

CRenderPreset* CRenderHelper::Preset(unsigned index) {
    return index < CRenderHelper::kPresetCount ? presets_[index] : nullptr;
}
EI_REWRITTEN(0x004FDBA0, CRenderHelper::Preset);

// The result is a byte (a bool).
bool CRenderHelper::PresetStates(CRenderPreset* preset, StateVector* out) {
    auto* node = records_.Find(preset);
    if (!node) return false;
    AssignStates(out, node->value.states);
    return true;
}
EI_REWRITTEN(0x004FDBC0, CRenderHelper::PresetStates);

// The result is a byte (a bool).
bool CRenderHelper::BuiltInStates(unsigned index, stlp::vector<RenderPresetRecord::State>* out) {
    if (index >= CRenderHelper::kPresetCount || !presets_[index]) return false;
    return PresetStates(presets_[index], out);
}
EI_REWRITTEN(0x004FDD80, CRenderHelper::BuiltInStates);

// ---- The preset -------------------------------------------------------------------------------------

CRenderPreset::CRenderPreset(const stlp::vector<std::uint32_t>& blocks, int stages, int index)
    : stages_(stages), index_(index) {
    CopyConstruct(&blocks_, blocks);
}

CRenderPreset::~CRenderPreset() { blocks_.FreeStorage(); }
EI_REWRITTEN_SPECIAL(0x004FD8B0, CRenderPreset::~CRenderPreset);
EI_REWRITTEN_SPECIAL(0x004FD880, CRenderPreset::~CRenderPreset);

void* CRenderPreset::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CRenderPreset::operator delete(void* block) { gamecrt::operator_delete(block); }

// The helper's and the preset's abstract bases have no fields; their deleting destructors,
// which no object reaches, are not modelled.

// ---- The transform pipeline (CTransformPipeline) -------------------------------------------------------------

MatrixStack::MatrixStack() : matrices(lib::AlignedArray<Matrix4>::Allocate(kMatrixStackDepth)), count(0) {}
EI_REWRITTEN_SPECIAL(0x004F0DB0, MatrixStack::MatrixStack);

MatrixStack::~MatrixStack() { lib::FreeAligned32(matrices); }
EI_REWRITTEN_SPECIAL(0x004F0DE0, MatrixStack::~MatrixStack);

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
EI_REWRITTEN_SPECIAL(0x004F0B90, CTransformPipeline::CTransformPipeline);

// The stacks' blocks go after the body (members), from the last to the first.
CTransformPipeline::~CTransformPipeline() {
    void* library = library_;
    transform_ = nullptr;
    if (library) gameapi::FreeLibrary(static_cast<HMODULE>(library));
    library_ = nullptr;
}
EI_REWRITTEN_SPECIAL(0x004F0E10, CTransformPipeline::~CTransformPipeline);
EI_REWRITTEN_SPECIAL(0x004F0DF0, CTransformPipeline::~CTransformPipeline);

// ---- The driver list --------------------------------------------------------------------------------

