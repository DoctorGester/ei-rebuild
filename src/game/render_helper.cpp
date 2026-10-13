// The renderer's helper object (CRenderHelper, model.h; vtable 0073D840: the Direct3D state blocks
// behind the material presets) and the old renderer's other scalar deleting destructors (A-S11).
// Rewritten here: the helper (its constructor with the nine built-in presets' state lists, the
// destructor, the state blocks' recording and release, and the slots that read its tables), its
// preset's destructor, the Direct3D driver list's lookup by index, the "destructor body, then
// operator delete" shells of five classes, and the transform pipeline's constructor and destructor
// with its matrix stacks' constructor. The device object's and the DirectDraw holder's destructor bodies are not
// here. See docs/notes/renderer.md (section 12).
#include "dx_errors.h"
#include "model.h"
#include "processor_info.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_library.h"   // CxxThrowException

#include <d3d.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// Destructor bodies not rewritten here (thiscall, plain RET): the device object's 004E68A0 and the
// DirectDraw holder's 004EE330 (vtable 0073D770). The helper's 004FCD50 is below; its deleting
// destructor reaches it through its address.
__declspec(naked) void __fastcall DeviceDestructOriginal(void*) EI_ORIGINAL(0x004E68A0)
__declspec(naked) void __fastcall DirectDrawHolderDestructOriginal(void*) EI_ORIGINAL(0x004EE330)
__declspec(naked) void __fastcall HelperDestructOriginal(void*) EI_ORIGINAL(0x004FCD50)

using State = RenderPresetRecord::State;
using StateVector = stlp::vector<State>;

// The helper's own calls of its constructor (through their addresses): 004FCE90 the state list of
// built-in preset `index` (RET 8, AL), 004FD2A0 slot 1 without the vtable (RET 0xC).
__declspec(naked) bool __fastcall BuiltInStateListOriginal(CRenderHelper*, void*, StateVector* /*states*/,
                                                           unsigned /*index*/) EI_ORIGINAL(0x004FCE90)
__declspec(naked) CRenderPreset* __fastcall RecordOriginal(CRenderHelper*, void*, const StateVector* /*states*/,
                                                           int /*index*/, std::uint32_t /*single stage (low byte)*/)
    EI_ORIGINAL(0x004FD2A0)

static_assert(sizeof(D3DDEVICEDESC7) == sizeof(CRenderHelper::caps_));
D3DDEVICEDESC7& Caps(CRenderHelper* self) { return *reinterpret_cast<D3DDEVICEDESC7*>(self->caps_); }

// The fatal errors' texts in the game's data.
const char* CantGetCapsText() { return reinterpret_cast<const char*>(0x0078EC90); }          // "Can't get device caps\n"
const char* AlreadyRegisteredText() { return reinterpret_cast<const char*>(0x0078ECA8); }    // "Can't register default effect %d\n Already exist"
const char* CantEndBlockText() { return reinterpret_cast<const char*>(0x0078ECD8); }         // "Can't end state block\n"
const char* CantBeginBlockText() { return reinterpret_cast<const char*>(0x0078ECF0); }       // "Can't begin state block\n"
const char* CantDeleteBlockText() { return reinterpret_cast<const char*>(0x0078ED0C); }      // "Can't delete state block %d\n"

// vector<T>::resize(count, value) as STLport writes it for plain elements (004FE6B0, 004FE0C0 and
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
        T* fresh = static_cast<T*>(stlp::allocate(capacity * sizeof(T)));
        if (size != 0) std::memcpy(fresh, v->Start(), size * sizeof(T));
        v->FreeStorage();
        v->Start() = fresh;
        v->Finish() = fresh + size;
        v->EndOfStorage() = fresh + capacity;
    }
    for (std::size_t i = 0; i < added; ++i) v->Finish()[i] = value;
    v->Finish() += added;
}

// vector<T>(source) for plain elements (004FDDB0, 004FE9A0): a block of exactly the source's length.
template <typename T>
void CopyConstruct(stlp::vector<T>* v, const stlp::vector<T>& source) {
    const std::size_t count = source.size();
    T* block = count != 0 ? static_cast<T*>(stlp::allocate(count * sizeof(T))) : nullptr;
    if (count != 0) std::memcpy(block, source.begin(), count * sizeof(T));
    v->Start() = block;
    v->Finish() = v->EndOfStorage() = block + count;
}

void Retain(CRenderPreset* preset) {
    if (preset) ++preset->refs_;
}
void Release(CRenderPreset* preset) {
    if (preset && --preset->refs_ == 0) preset->DeleteThis(1);
}

// 004FD920 / 004FDFE0: a record's destructor: the preset released, the states' storage freed.
void DestroyRecord(RenderPresetRecord* record) {
    Release(record->preset);
    record->states.FreeStorage();
}

// What a failed Direct3D call leaves as slot 1's result: the fatal error's (0 when it comes back).
CRenderPreset* Failed(int errorResult) { return reinterpret_cast<CRenderPreset*>(static_cast<std::uintptr_t>(errorResult)); }

// vector<State>::operator= as STLport writes it (004FDBC0 inlines it; the states are plain
// records): a source longer than the capacity gets a new block of exactly its length (the old one
// freed after the copy), otherwise the elements are copied over the old ones.
void AssignStates(StateVector* out, const StateVector& source) {
    using State = RenderPresetRecord::State;
    if (out == &source) return;
    const std::size_t count = source.size();
    const std::size_t capacity = static_cast<std::size_t>(out->EndOfStorage() - out->Start());
    if (count > capacity) {
        auto* fresh = static_cast<State*>(stlp::allocate(count * sizeof(State)));
        std::memcpy(fresh, source.begin(), count * sizeof(State));
        if (out->Start()) stlp::deallocate(out->Start(), capacity * sizeof(State));
        out->Start() = fresh;
        out->EndOfStorage() = fresh + count;
    } else if (count != 0) {
        std::memmove(out->Start(), source.begin(), count * sizeof(State));
    }
    out->Finish() = out->Start() + count;
}

constexpr int kMatrixStackDepth = 16;   // matrices a stack has room for (0x400 bytes)
constexpr int kMatrixStacks = 16;

void SetRootVtable(void* object) { ei::SetVtable(object, 0x0073C368); }

// The Direct3D driver list (an STLport list; 004EE4A0 fills it).
stlp::list<DirectDrawDriver>::Node* DriverListEnd() {
    return *reinterpret_cast<stlp::list<DirectDrawDriver>::Node* const*>(0x007AEE64);
}
const char* CantFindDriverText() { return reinterpret_cast<const char*>(0x0078E558); }   // "Can't find driver by index.\n"

void* DeleteIf(void* object, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(object);
    return object;
}

} // namespace

// ---- The built-in presets' states -------------------------------------------------------------------

namespace {

// A texture stage: the colour is `operation` of `argument` and the stage before, the alpha is
// switched off. The original fills the new elements from an uninitialised local with only the two
// flags cleared and then writes the fields below, so what a list does not set (stage 0's operations
// everywhere, the alpha arguments of presets 5-7) is stack contents there and zero here; slot 1
// passes the alpha arguments on to Direct3D even so.
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

// 004FCE90 (RET 8; only AL is the result). The state list of built-in preset `index` into `states`
// (emptied first); false beyond the nine. Presets 0 and 8 have no states.
bool __fastcall RenderHelperBuiltInStateList(CRenderHelper*, void*, StateVector* states, unsigned index) {
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
EI_REPLACE(0x004FCE90, RenderHelperBuiltInStateList);

// ---- State blocks -------------------------------------------------------------------------------------

// 004FD2A0 (slot 1, RET 0xC). The first block sets the texture stages of the first `stages` states
// (as many as the device has, or one) and switches the next stage off; each state from there on gets
// a block of its own with its blend factors (an empty block without them). The check of a stage's
// operations ands the operation's number with the device's capability bits, as the original does;
// a stage that fails it ends the first block there. Returns the new preset.
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

    HRESULT result = device_->BeginStateBlock();
    if (result < 0) {
        const int error = FatalDirectX(result, CantBeginBlockText());
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
        device_->SetTextureStageState(i, D3DTSS_COLOROP, state.colourOp);
        device_->SetTextureStageState(i, D3DTSS_COLORARG1, state.colourArg1);
        device_->SetTextureStageState(i, D3DTSS_COLORARG2, state.colourArg2);
        device_->SetTextureStageState(i, D3DTSS_ALPHAOP, state.alphaOp);
        device_->SetTextureStageState(i, D3DTSS_ALPHAARG1, state.alphaArg1);
        device_->SetTextureStageState(i, D3DTSS_ALPHAARG2, state.alphaArg2);
    }
    device_->SetTextureStageState(stages, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device_->SetTextureStageState(stages, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    DWORD block = 0;
    result = device_->EndStateBlock(&block);
    if (result < 0) {
        const int error = FatalDirectX(result, CantEndBlockText());
        blocks.FreeStorage();
        return Failed(error);
    }
    blocks[0] = block;

    for (std::size_t i = static_cast<std::size_t>(stages); i < states->size(); ++i) {
        result = device_->BeginStateBlock();
        if (result < 0) {
            const int error = FatalDirectX(result, CantBeginBlockText());
            blocks.FreeStorage();
            return Failed(error);
        }
        const State& state = (*states)[i];
        if (state.blend) {
            device_->SetRenderState(D3DRENDERSTATE_SRCBLEND, state.sourceBlend);
            device_->SetRenderState(D3DRENDERSTATE_DESTBLEND, state.destBlend);
        }
        result = device_->EndStateBlock(&block);
        if (result < 0) {
            const int error = FatalDirectX(result, CantEndBlockText());
            blocks.FreeStorage();
            return Failed(error);
        }
        blocks[i - stages + 1] = block;
    }

    auto* preset = static_cast<CRenderPreset*>(gamecrt::operator_new(sizeof(CRenderPreset)));
    preset->refs_ = 0;
    CopyConstruct(&preset->blocks_, blocks);
    preset->stages_ = stages;
    preset->index_ = index;
    ei::SetVtable(preset, 0x0073D880);

    if (index < static_cast<int>(kPresetCount)) {   // signed, as the original compares
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

CRenderPreset* __fastcall RenderHelperRecord(CRenderHelper* self, void*, const StateVector* states, int index,
                                             bool singleStage) {
    return self->CRenderHelper::Record(states, index, singleStage);
}
EI_REPLACE(0x004FD2A0, RenderHelperRecord);

// 004FD9D0 (slot 3, RET 4). The preset's state blocks deleted, its place among the built-in presets
// cleared, and its record erased (hashtable::erase(key) as STLport writes it: the bucket's later
// nodes first, then its head). The record holds the preset, so this usually deletes it.
void CRenderHelper::ReleaseBlock(CRenderPreset* preset) {
    for (int i = 0; i < static_cast<int>(preset->blocks_.size()); ++i) {
        const std::uint32_t block = preset->blocks_[i];
        const HRESULT result = device_->DeleteStateBlock(block);
        if (result < 0) FatalDirectX(result, CantDeleteBlockText(), block);
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

void __fastcall RenderHelperReleaseBlock(CRenderHelper* self, void*, CRenderPreset* preset) {
    self->CRenderHelper::ReleaseBlock(preset);
}
EI_REPLACE(0x004FD9D0, RenderHelperReleaseBlock);

// ---- The helper's lifetime ----------------------------------------------------------------------------

// 004FCA60 (RET 8; returns the helper). Takes the device and its capabilities, then records the nine
// built-in presets (`flag`'s low byte: one texture stage each).
CRenderHelper* __fastcall RenderHelperConstruct(CRenderHelper* self, void*, IDirect3DDevice7* device,
                                                std::uint32_t flag) {
    self->refs_ = 0;
    self->device_ = nullptr;
    self->records_.InitBuckets(100);
    self->presets_.Start() = self->presets_.Finish() = self->presets_.EndOfStorage() = nullptr;
    ei::SetVtable(self, 0x0073D840);

    self->device_ = device;
    if (device) device->AddRef();

    std::memset(self->caps_, 0, sizeof(self->caps_));
    const HRESULT result = self->device_->GetCaps(&Caps(self));
    if (result < 0) {
        FatalDirectX(result, CantGetCapsText());
        std::memset(self->caps_, 0, sizeof(self->caps_));
    }

    Resize(&self->presets_, CRenderHelper::kPresetCount, static_cast<CRenderPreset*>(nullptr));
    StateVector states;
    for (unsigned index = 0; index < CRenderHelper::kPresetCount; ++index) {
        if (BuiltInStateListOriginal(self, nullptr, &states, index)) {
            RecordOriginal(self, nullptr, &states, static_cast<int>(index), flag);
        }
    }
    states.FreeStorage();
    return self;
}
EI_REPLACE(0x004FCA60, RenderHelperConstruct);

// 004FCD50: the destructor body. Every record's preset goes through slot 3, which erases the record
// the walk stands on: as in the original, the step to the next record then reads the freed node (its
// link is the node allocator's free list by then), so the walk can stray and leave records behind.
// Those are destroyed with the table (their presets deleted, but not their state blocks).
void __fastcall RenderHelperDestruct(CRenderHelper* self) {
    using Node = decltype(self->records_)::Node;
    ei::SetVtable(self, 0x0073D840);
    for (Node* node = self->records_.First(); node; node = self->records_.After(node)) {
        if (Node* found = self->records_.Find(node->key)) self->ReleaseBlock(found->value.preset);
    }
    self->presets_.FreeStorage();
    self->records_.ClearWith([](Node* node) {
        DestroyRecord(&node->value);
        stlp::deallocate(node, sizeof(Node));
    });
    self->records_.Destroy();
    if (self->device_) self->device_->Release();
    SetRootVtable(self);
}
EI_REPLACE(0x004FCD50, RenderHelperDestruct);

// ---- The helper's table slots -----------------------------------------------------------------------

// 004FDB00 (slot 2, RET 4).
void __fastcall RenderHelperReleasePreset(CRenderHelper* self, void*, CRenderPreset* preset) {
    if (auto* node = self->records_.Find(preset)) self->ReleaseBlock(node->value.preset);
}
EI_REPLACE(0x004FDB00, RenderHelperReleasePreset);

// 004FDB50 (slot 4, RET 4).
void* __fastcall RenderHelperPresetBlock(CRenderHelper* self, void*, CRenderPreset* preset) {
    auto* node = self->records_.Find(preset);
    return node ? node->value.preset : nullptr;
}
EI_REPLACE(0x004FDB50, RenderHelperPresetBlock);

// 004FDBA0 (slot 5, RET 4).
CRenderPreset* __fastcall RenderHelperPreset(CRenderHelper* self, void*, unsigned index) {
    return index < CRenderHelper::kPresetCount ? self->presets_[index] : nullptr;
}
EI_REPLACE(0x004FDBA0, RenderHelperPreset);

// 004FDBC0 (slot 6, RET 8). Only AL is the result.
bool __fastcall RenderHelperPresetStates(CRenderHelper* self, void*, CRenderPreset* preset, StateVector* out) {
    auto* node = self->records_.Find(preset);
    if (!node) return false;
    AssignStates(out, node->value.states);
    return true;
}
EI_REPLACE(0x004FDBC0, RenderHelperPresetStates);

// 004FDD80 (slot 7, RET 8). Only AL is the result.
bool __fastcall RenderHelperBuiltInStates(CRenderHelper* self, void*, unsigned index,
                                          stlp::vector<RenderPresetRecord::State>* out) {
    if (index >= CRenderHelper::kPresetCount || !self->presets_[index]) return false;
    return self->PresetStates(self->presets_[index], out);
}
EI_REPLACE(0x004FDD80, RenderHelperBuiltInStates);

// ---- The preset -------------------------------------------------------------------------------------

// 004FD8B0: the preset's destructor body: its vector's storage freed, the root's vtable left.
void __fastcall RenderPresetDestruct(CRenderPreset* self) {
    self->blocks_.FreeStorage();
    SetRootVtable(self);
}
EI_REPLACE(0x004FD8B0, RenderPresetDestruct);

// 004FD880 (slot 0, RET 4).
void* __fastcall RenderPresetDelete(CRenderPreset* self, void*, unsigned flags) {
    RenderPresetDestruct(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004FD880, RenderPresetDelete);

// ---- Deleting destructors (slot 0, RET 4) -------------------------------------------------------------

// 004FCD30 and 004FD900: the helper's and the preset's abstract base classes (vtables 0073D860 /
// 0073D888): the root's vtable left (004FCC40 / 004FD8A0, inlined). No object keeps those vtables
// while it can be released, so nothing calls them.
void* __fastcall RenderBaseDelete(void* self, void*, unsigned flags) {
    SetRootVtable(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004FCD30, RenderBaseDelete);
EI_REPLACE(0x004FD900, RenderBaseDelete);

// 004E6880: the device object's (vtable 0073D760).
void* __fastcall DeviceDelete(void* self, void*, unsigned flags) {
    DeviceDestructOriginal(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004E6880, DeviceDelete);

// 004EE310: the DirectDraw holder's.
void* __fastcall DirectDrawHolderDelete(void* self, void*, unsigned flags) {
    DirectDrawHolderDestructOriginal(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004EE310, DirectDrawHolderDelete);

// 004FCC20: the helper's (the body above, through its address).
void* __fastcall RenderHelperDelete(void* self, void*, unsigned flags) {
    HelperDestructOriginal(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004FCC20, RenderHelperDelete);

// ---- The transform pipeline (CTransformPipeline, vtable 0073D778) -----------------------------------------

// 004F0DB0: a matrix stack's constructor (the pipeline's constructor 004F0B90 has the compiler's
// array constructor run it for each of the 16): room for 16 matrices, empty.
MatrixStack* __fastcall MatrixStackConstruct(MatrixStack* self) {
    self->matrices = lib::AlignedArray<Matrix4>::Allocate(kMatrixStackDepth);
    self->count = 0;
    return self;
}
EI_REPLACE(0x004F0DB0, MatrixStackConstruct);

namespace {

// 004F0DB0 through its address: the verify build's wrapper runs there.
__declspec(naked) MatrixStack* __fastcall MatrixStackConstructOriginal(MatrixStack*) EI_ORIGINAL(0x004F0DB0)

const char* TransformLibraryName() { return reinterpret_cast<const char*>(0x0078EC0C); }   // "3DfpFPU.dll"
const char* TransformFunctionName() { return reinterpret_cast<const char*>(0x0078EBD4); }  // "GetTLPipeline"
// The debug messages ("FPU Transform release library loaded." and "Acquiring geometry processor
// from transform library.") and the errors' texts ("Can't load 3D transform library." and "Loaded
// 3D transform library have no function "GetTLPipeline"."), each with a newline.
const char* LibraryLoadedText() { return reinterpret_cast<const char*>(0x0078EBE4); }
const char* AcquiringText() { return reinterpret_cast<const char*>(0x0078EB9C); }
const char* NoLibraryText() { return reinterpret_cast<const char*>(0x0078EB38); }
const char* NoFunctionText() { return reinterpret_cast<const char*>(0x0078EB5C); }

// The constructor's failure: a new error object thrown (a GameError*, by the game's own throw).
// What the original's unwinding does on the way out is done here first: the folder's string and
// the stacks' blocks (from the last) freed, the counted base's vtable left. (The memory itself is
// the caller's: the original's `new` frees it.)
[[noreturn]] void ThrowPipelineError(CTransformPipeline* self, std::optional<stlp::string>& folder, const char* text) {
    void* memory = gamecrt::operator_new(sizeof(GameError));
    GameError* error = memory ? GameErrorConstruct(static_cast<GameError*>(memory), text) : nullptr;
    folder.reset();
    for (int i = kMatrixStacks - 1; i >= 0; --i) lib::FreeAligned32(self->stacks_[i].matrices);
    ei::SetVtable(self, 0x0073D768);
    CxxThrowException(&error, GameErrorThrowInfo());
    for (;;) {
    }
}

} // namespace

// 004F0B90 (RET 4; the renderer base's creation 00500540 news it with the game's work folder): the
// counted base, the 16 matrix stacks, the processor's CPUID function 1 into the globals (as the
// renderer does again), then 3DfpFPU.dll loaded from `folder` and its GetTLPipeline()'s transform
// object taken. No library, or no such function in it (the library is unloaded again): throws.
CTransformPipeline* __fastcall TransformPipelineConstruct(CTransformPipeline* self, void*, const char* folder) {
    self->refs_ = 0;
    std::memset(self->unknown_08, 0, sizeof(self->unknown_08));
    ei::SetVtable(self, 0x0073D768);
    for (int i = 0; i < kMatrixStacks; ++i) MatrixStackConstructOriginal(&self->stacks_[i]);
    ei::SetVtable(self, 0x0073D778);

    std::optional<stlp::string> path;   // freed by hand before a throw
    path.emplace(folder);
    processor::Read();
    self->library_ = gameapi::LoadLibraryA(stlp::Plus(*path, TransformLibraryName()).data());
    DebugMessageOriginal(LibraryLoadedText());
    if (!self->library_) ThrowPipelineError(self, path, NoLibraryText());

    using GetTLPipeline = ITransformPipeline*(__cdecl*)();
    auto getPipeline = reinterpret_cast<GetTLPipeline>(
        gameapi::GetProcAddress(static_cast<HMODULE>(self->library_), TransformFunctionName()));
    if (!getPipeline) {
        gameapi::FreeLibrary(static_cast<HMODULE>(self->library_));
        self->library_ = nullptr;
        ThrowPipelineError(self, path, NoFunctionText());
    }
    DebugMessageOriginal(AcquiringText());
    self->transform_ = getPipeline();
    return self;
}
EI_REPLACE(0x004F0B90, TransformPipelineConstruct);

// 004F0E10: the destructor body: the transform object forgotten, 3DfpFPU.dll unloaded, the stacks'
// blocks freed from the last to the first (004F0DE0 each), the counted base's vtable left.
void __fastcall TransformPipelineDestruct(CTransformPipeline* self) {
    ei::SetVtable(self, 0x0073D778);
    void* library = self->library_;
    self->transform_ = nullptr;
    if (library) gameapi::FreeLibrary(static_cast<HMODULE>(library));
    self->library_ = nullptr;
    for (int i = kMatrixStacks - 1; i >= 0; --i) lib::FreeAligned32(self->stacks_[i].matrices);
    ei::SetVtable(self, 0x0073D768);
}
EI_REPLACE(0x004F0E10, TransformPipelineDestruct);

// 004F0DF0 (slot 0, RET 4).
void* __fastcall TlLibraryDelete(void* self, void*, unsigned flags) {
    TransformPipelineDestruct(static_cast<CTransformPipeline*>(self));
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004F0DF0, TlLibraryDelete);

// ---- The driver list --------------------------------------------------------------------------------

// 004EE6D0 (the index in ECX). The driver `index` steps from the list's first (a negative index
// walks backwards from it, through the end node); the end node itself is a fatal error and null.
DirectDrawDriver* __fastcall DriverAt(int index) {
    auto* end = DriverListEnd();
    auto* node = end->next;
    for (; index > 0; --index) node = node->next;
    for (; index < 0; ++index) node = node->prev;
    if (node != end) return &node->data;
    game::FatalError(CantFindDriverText());
    return nullptr;
}
EI_REPLACE(0x004EE6D0, DriverAt);
