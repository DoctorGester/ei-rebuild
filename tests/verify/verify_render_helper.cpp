// Checks for render_helper.cpp (A-S11). Main thread.
//  - Side by side (pure): the helper's preset lookups 004FDB50 / 004FDBA0 (the material presets:
//    shadows and wounds ask for theirs in any scene with units), a preset's states 004FDBC0 (the
//    original into a scratch vector, compared by content; no scenario is known to ask for them:
//    the callers are the helper's own slots 1 and 7) and the driver list's lookup
//    004EE6D0 for an index inside the list (the renderer's creation with a configured driver;
//    beyond the list it is a fatal error, which is run once: ours only).
//  - Ours only: the slots that call other slots (004FDB00 releases a state block, 004FDD80 copies a
//    vector), the preset's destructor 004FD8B0 / 004FD880 and the deleting destructors of the
//    device object, the DirectDraw holder, the helper and the TL library holder (the renderer's
//    shutdown: every exit). The two base classes' deleting destructors 004FCD30 / 004FD900 have no
//    caller. The transform pipeline's matrix stack constructor 004F0DB0 (the renderer's creation,
//    every start: 16 of them) and destructor body 004F0E10 (it unloads 3DfpFPU.dll; its only
//    caller is the deleting destructor 004F0DF0, whose rewrite calls ours directly at the
//    renderer's shutdown, so the wrapper itself is not reached).
//    The pipeline's constructor 004F0B90 (A-S12; the renderer's creation, every start) is side by
//    side: see at the wrapper.
// A-S12, the rest of the helper (the renderer's creation, every start, and its shutdown):
//  - Side by side: the built-in presets' state lists 004FCE90 (the original into a scratch vector;
//    the fields a list sets must agree, the ones it leaves unset are stack contents there).
//  - Side by side on a recording device: at the helper's construction the original and the rewrite
//    each build a second helper on a fake IDirect3DDevice7 that only notes its calls (with the real
//    device's capabilities and the given flag, with the other flag, and with capabilities that fail
//    the stage check). Compared: the calls the constructor 004FCA60 and slot 1 004FD2A0 make, the
//    helpers they leave (presets, blocks, records), the calls and tables of slot 3 004FD9D0 for
//    eight of the presets, and the destructor 004FCD50 with the last one (its walk ends there).
//  - Ours only, on the real device: the same four (the state blocks cannot be recorded twice), with
//    the tables checked afterwards. The destructor's walk over more than one record reads freed
//    nodes (an original bug that is kept), which depends on the allocator's free list and cannot be
//    compared.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/sys_info.h"
#include "../../src/game/texture.h"
#include "../../src/lib/containers.h"
#include "../../src/lib/gameapi.h"
#include "../../src/lib/stlp.h"

#include <d3d.h>

#include <cstdint>
#include <cstring>
#include <vector>

// render_helper.cpp (no header declares the shims).
bool __fastcall RenderHelperPresetStates(CRenderHelper* self, void*, CRenderPreset* preset,
                                         stlp::vector<RenderPresetRecord::State>* out);
MatrixStack* __fastcall MatrixStackConstruct(MatrixStack* self);
void __fastcall TransformPipelineDestruct(CTransformPipeline* self);
void __fastcall RenderHelperReleasePreset(CRenderHelper* self, void*, CRenderPreset* preset);
void* __fastcall RenderHelperPresetBlock(CRenderHelper* self, void*, CRenderPreset* preset);
CRenderPreset* __fastcall RenderHelperPreset(CRenderHelper* self, void*, unsigned index);
bool __fastcall RenderHelperBuiltInStates(CRenderHelper* self, void*, unsigned index,
                                          stlp::vector<RenderPresetRecord::State>* out);
void __fastcall RenderPresetDestruct(CRenderPreset* self);
void* __fastcall RenderPresetDelete(CRenderPreset* self, void*, unsigned flags);
void* __fastcall RenderBaseDelete(void* self, void*, unsigned flags);
void* __fastcall DeviceDelete(void* self, void*, unsigned flags);
void* __fastcall DirectDrawHolderDelete(void* self, void*, unsigned flags);
void* __fastcall RenderHelperDelete(void* self, void*, unsigned flags);
void* __fastcall TlLibraryDelete(void* self, void*, unsigned flags);
CRenderPreset* __fastcall RenderHelperRecord(CRenderHelper* self, void*,
                                             const stlp::vector<RenderPresetRecord::State>* states, int index,
                                             bool singleStage);
void __fastcall RenderHelperReleaseBlock(CRenderHelper* self, void*, CRenderPreset* preset);
void __fastcall RenderHelperDestruct(CRenderHelper* self);

namespace {
void* s_unused;

// ---- The helper's state blocks (A-S12) ----

using State = RenderPresetRecord::State;
using StateVector = stlp::vector<State>;

// Set while an original runs on the fake device: the wrappers of what it calls pass straight through.
bool s_original;

// 004FCE90: SUB ESP,0x28 (3), PUSH EBX (1), PUSH EBP (1). 004FD2A0: MOV EAX,FS:[0] (6).
// 004FD9D0: PUSH ECX (1), PUSH EBX (1), MOV EBX,[ESP+0xC] (4). 004FCA60 / 004FCD50: PUSH -1 (2),
// PUSH handler (5).
bool(__fastcall* s_stateList)(CRenderHelper*, void*, StateVector*, unsigned);
CRenderPreset*(__fastcall* s_record)(CRenderHelper*, void*, const StateVector*, int, bool);
void(__fastcall* s_releaseBlock)(CRenderHelper*, void*, CRenderPreset*);
CRenderHelper*(__fastcall* s_construct)(CRenderHelper*, void*, IDirect3DDevice7*, std::uint32_t);
void(__fastcall* s_destruct)(CRenderHelper*);

// The fields a state list sets agree; what it does not set is stack contents in the original and
// zero in the rewrite (the alpha arguments of presets 5-7 are the only ones that reach Direct3D).
bool SameState(const State& theirs, const State& ours) {
    if (theirs.enabled != ours.enabled || theirs.blend != ours.blend) return false;
    if (ours.enabled && (ours.colourOp != theirs.colourOp || ours.colourArg1 != theirs.colourArg1 ||
                         ours.colourArg2 != theirs.colourArg2 || ours.alphaOp != theirs.alphaOp))
        return false;
    if (ours.alphaArg1 != 0 && ours.alphaArg1 != theirs.alphaArg1) return false;
    if (ours.alphaArg2 != 0 && ours.alphaArg2 != theirs.alphaArg2) return false;
    if (ours.blend && (ours.sourceBlend != theirs.sourceBlend || ours.destBlend != theirs.destBlend)) return false;
    return true;
}
bool SameStates(const StateVector& theirs, const StateVector& ours) {
    if (theirs.size() != ours.size()) return false;
    for (std::size_t i = 0; i < ours.size(); ++i)
        if (!SameState(theirs[i], ours[i])) return false;
    return true;
}

bool __fastcall StateListVerify(CRenderHelper* self, void*, StateVector* states, unsigned index) {
    if (s_original) return s_stateList(self, nullptr, states, index);
    StateVector expected;
    bool expectedResult = s_stateList(self, nullptr, &expected, index);
    bool result = self->RenderHelperBuiltInStateList(states, index);
    ei::verify::Check("CRenderHelper built-in states (004FCE90)", true,
                      expectedResult == result && SameStates(expected, *states), "preset %u: %d, %u states", index,
                      result ? 1 : 0, static_cast<unsigned>(states->size()));
    expected.FreeStorage();
    return result;
}

// What slot 1 must leave: the preset with its blocks, in the table and (below 9) among the built-in ones.
bool Recorded(CRenderHelper* self, const StateVector* states, int index, CRenderPreset* preset) {
    if (!preset || preset->index_ != index || preset->stages_ < 0) return false;
    const std::size_t count = states->size();
    if (static_cast<std::size_t>(preset->stages_) > count) return false;
    if (preset->blocks_.size() != count - preset->stages_ + 1) return false;
    auto* node = self->records_.Find(preset);
    if (!node || node->value.preset != preset || node->value.states.size() != count) return false;
    if (count != 0 && std::memcmp(node->value.states.begin(), states->begin(), count * sizeof(State)) != 0) return false;
    return index < 0 || index >= static_cast<int>(CRenderHelper::kPresetCount) || self->presets_[index] == preset;
}

CRenderPreset* __fastcall RecordVerify(CRenderHelper* self, void*, const StateVector* states, int index,
                                       bool singleStage) {
    if (s_original) return s_record(self, nullptr, states, index, singleStage);
    CRenderPreset* result = RenderHelperRecord(self, nullptr, states, index, singleStage);
    ei::verify::Check("CRenderHelper slot 1 (004FD2A0)", true, Recorded(self, states, index, result),
                      "preset %d: %u states, %d stages", index, static_cast<unsigned>(states->size()),
                      result ? result->stages_ : -1);
    return result;
}

void __fastcall ReleaseBlockVerify(CRenderHelper* self, void*, CRenderPreset* preset) {
    if (s_original) {
        s_releaseBlock(self, nullptr, preset);
        return;
    }
    const std::size_t before = self->records_.Size();
    const bool known = self->records_.Find(preset) != nullptr;
    RenderHelperReleaseBlock(self, nullptr, preset);
    const bool gone = self->records_.Find(preset) == nullptr && self->records_.Size() == before - (known ? 1 : 0);
    ei::verify::Check("CRenderHelper slot 3 (004FD9D0)", true, gone, "known %d, %u records left", known ? 1 : 0,
                      static_cast<unsigned>(self->records_.Size()));
}

void __fastcall DestructVerify(CRenderHelper* self) {
    if (s_original) {
        s_destruct(self);
        return;
    }
    RenderHelperDestruct(self);
    ei::verify::Check("CRenderHelper destructor (004FCD50)", true, true, "");
}

// -- The recording device --

enum class CallKind { AddRef, Release, GetCaps, Begin, End, Stage, Render, Delete, Unexpected };
struct DeviceCall {
    CallKind kind;
    std::uint32_t a, b, c;
};
struct FakeDevice {
    void** vtable;
    D3DDEVICEDESC7 caps;
    int refs;
    std::uint32_t blocks;   // handles given out
    std::vector<DeviceCall> calls;
};

HRESULT __stdcall FakeUnexpected(FakeDevice* self) {
    self->calls.push_back({CallKind::Unexpected, 0, 0, 0});
    return E_NOTIMPL;
}
ULONG __stdcall FakeAddRef(FakeDevice* self) {
    self->calls.push_back({CallKind::AddRef, 0, 0, 0});
    return static_cast<ULONG>(++self->refs);
}
ULONG __stdcall FakeRelease(FakeDevice* self) {
    self->calls.push_back({CallKind::Release, 0, 0, 0});
    return static_cast<ULONG>(--self->refs);
}
HRESULT __stdcall FakeGetCaps(FakeDevice* self, D3DDEVICEDESC7* caps) {
    self->calls.push_back({CallKind::GetCaps, 0, 0, 0});
    *caps = self->caps;
    return 0;
}
HRESULT __stdcall FakeSetRenderState(FakeDevice* self, std::uint32_t state, std::uint32_t value) {
    self->calls.push_back({CallKind::Render, state, value, 0});
    return 0;
}
HRESULT __stdcall FakeBeginStateBlock(FakeDevice* self) {
    self->calls.push_back({CallKind::Begin, 0, 0, 0});
    return 0;
}
HRESULT __stdcall FakeEndStateBlock(FakeDevice* self, std::uint32_t* block) {
    *block = ++self->blocks;
    self->calls.push_back({CallKind::End, *block, 0, 0});
    return 0;
}
HRESULT __stdcall FakeSetTextureStageState(FakeDevice* self, std::uint32_t stage, std::uint32_t type,
                                           std::uint32_t value) {
    self->calls.push_back({CallKind::Stage, stage, type, value});
    return 0;
}
HRESULT __stdcall FakeDeleteStateBlock(FakeDevice* self, std::uint32_t block) {
    self->calls.push_back({CallKind::Delete, block, 0, 0});
    return 0;
}

// IDirect3DDevice7's slots the helper calls.
constexpr int kDeviceSlots = 49;
void** FakeVtable() {
    static void* slots[kDeviceSlots];
    if (!slots[0]) {
        for (void*& slot : slots) slot = reinterpret_cast<void*>(&FakeUnexpected);
        slots[1] = reinterpret_cast<void*>(&FakeAddRef);
        slots[2] = reinterpret_cast<void*>(&FakeRelease);
        slots[3] = reinterpret_cast<void*>(&FakeGetCaps);
        slots[20] = reinterpret_cast<void*>(&FakeSetRenderState);
        slots[22] = reinterpret_cast<void*>(&FakeBeginStateBlock);
        slots[23] = reinterpret_cast<void*>(&FakeEndStateBlock);
        slots[37] = reinterpret_cast<void*>(&FakeSetTextureStageState);
        slots[41] = reinterpret_cast<void*>(&FakeDeleteStateBlock);
    }
    return slots;
}

// The same calls; an alpha argument the rewrite leaves at zero is one the original never set.
bool SameCalls(const std::vector<DeviceCall>& theirs, const std::vector<DeviceCall>& ours) {
    if (theirs.size() != ours.size()) return false;
    for (std::size_t i = 0; i < ours.size(); ++i) {
        const DeviceCall& t = theirs[i];
        const DeviceCall& o = ours[i];
        if (t.kind == CallKind::Unexpected || t.kind != o.kind || t.a != o.a || t.b != o.b) return false;
        const bool unsetAlpha =
            o.kind == CallKind::Stage && (o.b == D3DTSS_ALPHAARG1 || o.b == D3DTSS_ALPHAARG2) && o.c == 0;
        if (!unsetAlpha && t.c != o.c) return false;
    }
    return true;
}

template <typename T> bool SameWords(const stlp::vector<T>& a, const stlp::vector<T>& b) {
    return a.size() == b.size() && (a.size() == 0 || std::memcmp(a.begin(), b.begin(), a.size() * sizeof(T)) == 0);
}
template <typename T> std::size_t Capacity(stlp::vector<T>& v) {
    return static_cast<std::size_t>(v.EndOfStorage() - v.Start());
}

// Two helpers built from the same calls: null when they hold the same, else what differs.
const char* HelpersDiffer(CRenderHelper* theirs, CRenderHelper* ours) {
    if (*reinterpret_cast<void**>(theirs) != *reinterpret_cast<void**>(ours)) return "vtable";
    if (theirs->refs_ != ours->refs_ || theirs->device_ != ours->device_) return "count or device";
    if (std::memcmp(theirs->caps_, ours->caps_, sizeof(ours->caps_)) != 0) return "capabilities";
    if (theirs->records_.Size() != ours->records_.Size() ||
        theirs->records_.BucketCount() != ours->records_.BucketCount())
        return "table size";
    if (theirs->presets_.size() != ours->presets_.size() || Capacity(theirs->presets_) != Capacity(ours->presets_))
        return "preset list";
    for (std::size_t i = 0; i < ours->presets_.size(); ++i) {
        CRenderPreset* a = theirs->presets_[i];
        CRenderPreset* b = ours->presets_[i];
        if (!a != !b) return "preset presence";
        if (!a) continue;
        if (*reinterpret_cast<void**>(a) != *reinterpret_cast<void**>(b)) return "preset vtable";
        if (a->refs_ != b->refs_ || a->stages_ != b->stages_ || a->index_ != b->index_) return "preset fields";
        if (!SameWords(a->blocks_, b->blocks_) || Capacity(a->blocks_) != Capacity(b->blocks_)) return "preset blocks";
        auto* na = theirs->records_.Find(a);
        auto* nb = ours->records_.Find(b);
        if (!na || !nb || na->value.preset != a || nb->value.preset != b) return "record";
        if (!SameStates(na->value.states, nb->value.states) ||
            Capacity(na->value.states) != Capacity(nb->value.states))
            return "record states";
    }
    return nullptr;
}

// The original and the rewrite each build, use and destroy a helper on the recording device.
void CompareOnFakeDevice(const D3DDEVICEDESC7& caps, std::uint32_t flag, const char* label) {
    FakeDevice fake{FakeVtable(), caps, 0, 0, {}};
    auto* device = reinterpret_cast<IDirect3DDevice7*>(&fake);
    alignas(4) std::uint8_t memoryA[sizeof(CRenderHelper)];
    alignas(4) std::uint8_t memoryB[sizeof(CRenderHelper)];
    auto* theirs = reinterpret_cast<CRenderHelper*>(memoryA);
    auto* ours = reinterpret_cast<CRenderHelper*>(memoryB);
    const char* wrong = nullptr;
    auto note = [&](const char* what) {
        if (!wrong) wrong = what;
    };

    s_original = true;
    s_construct(theirs, nullptr, device, flag);
    s_original = false;
    std::vector<DeviceCall> expected = std::move(fake.calls);
    fake.calls.clear();
    fake.blocks = 0;
    ours->RenderHelperConstruct(device, flag);
    if (!SameCalls(expected, fake.calls)) note("constructor calls");
    if (const char* differ = HelpersDiffer(theirs, ours)) note(differ);

    // Slot 3 for eight presets, the three-state one (5) left for the destructor.
    static const unsigned kOrder[] = {4, 0, 8, 1, 7, 2, 6, 3};
    for (unsigned index : kOrder) {
        CRenderPreset* a = theirs->presets_[index];
        CRenderPreset* b = ours->presets_[index];
        if (!a || !b) {
            note("preset missing");
            continue;
        }
        fake.calls.clear();
        s_original = true;
        s_releaseBlock(theirs, nullptr, a);
        s_original = false;
        expected = std::move(fake.calls);
        fake.calls.clear();
        RenderHelperReleaseBlock(ours, nullptr, b);
        if (!SameCalls(expected, fake.calls)) note("slot 3 calls");
        if (theirs->presets_[index] || ours->presets_[index] || theirs->records_.Size() != ours->records_.Size() ||
            theirs->records_.Find(a) || ours->records_.Find(b))
            note("slot 3 tables");
    }

    fake.calls.clear();
    s_original = true;
    s_destruct(theirs);
    s_original = false;
    expected = std::move(fake.calls);
    fake.calls.clear();
    RenderHelperDestruct(ours);
    if (!SameCalls(expected, fake.calls)) note("destructor calls");
    if (*reinterpret_cast<void**>(theirs) != *reinterpret_cast<void**>(ours) || fake.refs != 0) note("destructor");

    ei::verify::Check("CRenderHelper on a recording device (004FCA60, 004FD2A0, 004FD9D0, 004FCD50)", true,
                      wrong == nullptr, "%s: %s", label, wrong ? wrong : "same");
}

CRenderHelper* __fastcall ConstructVerify(CRenderHelper* self, void*, IDirect3DDevice7* device, std::uint32_t flag) {
    if (s_original) return s_construct(self, nullptr, device, flag);
    CRenderHelper* result = self->RenderHelperConstruct(device, flag);
    bool good = result == self && self->device_ == device && self->presets_.size() == CRenderHelper::kPresetCount &&
                self->records_.Size() == CRenderHelper::kPresetCount;
    for (unsigned i = 0; good && i < CRenderHelper::kPresetCount; ++i) {
        CRenderPreset* preset = self->presets_[i];
        good = preset && preset->refs_ == 1 && preset->index_ == static_cast<int>(i) && self->records_.Find(preset);
    }
    ei::verify::Check("CRenderHelper constructor (004FCA60)", true, good, "flag %u", flag & 0xFF);

    D3DDEVICEDESC7 caps;
    std::memcpy(&caps, self->caps_, sizeof(caps));
    CompareOnFakeDevice(caps, flag, "the device's capabilities");
    CompareOnFakeDevice(caps, flag ^ 1, "the other flag");
    caps.dwTextureOpCaps = 0x11;   // operations 4 (presets 1 and 2) fail the check
    caps.wMaxSimultaneousTextures = 8;
    CompareOnFakeDevice(caps, flag & ~0xFFu, "few operations");
    return result;
}

// ---- The helper's tables ----

void __fastcall ReleasePresetVerify(CRenderHelper* self, void*, CRenderPreset* preset) {
    bool known = self->records_.Find(preset) != nullptr;
    RenderHelperReleasePreset(self, nullptr, preset);
    ei::verify::Check("CRenderHelper slot 2 (004FDB00)", true, true, "known %d", known ? 1 : 0);
}

// 004FDB50: PUSH ESI (1), MOV ESI,[ECX+0xFC] (6).
void*(__fastcall* s_presetBlock)(CRenderHelper*, void*, CRenderPreset*);
void* __fastcall PresetBlockVerify(CRenderHelper* self, void*, CRenderPreset* preset) {
    void* expected = s_presetBlock(self, nullptr, preset);
    void* result = RenderHelperPresetBlock(self, nullptr, preset);
    ei::verify::Check("CRenderHelper slot 4 (004FDB50)", expected, result, "preset %p", static_cast<void*>(preset));
    return result;
}

// 004FDBA0: MOV EAX,[ESP+4] (4), CMP EAX,9 (3).
CRenderPreset*(__fastcall* s_preset)(CRenderHelper*, void*, unsigned);
CRenderPreset* __fastcall PresetVerify(CRenderHelper* self, void*, unsigned index) {
    CRenderPreset* expected = s_preset(self, nullptr, index);
    CRenderPreset* result = RenderHelperPreset(self, nullptr, index);
    ei::verify::Check("CRenderHelper::Preset (004FDBA0)", static_cast<void*>(expected), static_cast<void*>(result),
                      "index %u", index);
    return result;
}

// 004FDBC0: SUB ESP,0xC (3), ADD ECX,0xF8 (6).
bool(__fastcall* s_presetStates)(CRenderHelper*, void*, CRenderPreset*, stlp::vector<RenderPresetRecord::State>*);
bool __fastcall PresetStatesVerify(CRenderHelper* self, void*, CRenderPreset* preset,
                                   stlp::vector<RenderPresetRecord::State>* out) {
    stlp::vector<RenderPresetRecord::State> expected;
    bool expectedResult = s_presetStates(self, nullptr, preset, &expected);
    bool result = RenderHelperPresetStates(self, nullptr, preset, out);
    bool same = expectedResult == result;
    if (same && result) {
        same = expected.size() == out->size() &&
               (expected.size() == 0 ||
                std::memcmp(expected.begin(), out->begin(), expected.size() * sizeof(RenderPresetRecord::State)) == 0);
    }
    ei::verify::Check("CRenderHelper slot 6 (004FDBC0)", true, same, "preset %p: %d, %u states",
                      static_cast<void*>(preset), result ? 1 : 0, static_cast<unsigned>(out->size()));
    expected.FreeStorage();
    return result;
}

bool __fastcall BuiltInStatesVerify(CRenderHelper* self, void*, unsigned index,
                                    stlp::vector<RenderPresetRecord::State>* out) {
    bool possible = index < CRenderHelper::kPresetCount && self->presets_[index] != nullptr;
    bool result = RenderHelperBuiltInStates(self, nullptr, index, out);
    ei::verify::Check("CRenderHelper slot 7 (004FDD80)", true, possible || !result, "index %u result %d", index,
                      result ? 1 : 0);
    return result;
}

// ---- Destructors ----

void __fastcall PresetDestructVerify(CRenderPreset* self) {
    RenderPresetDestruct(self);
    ei::verify::Check("CRenderPreset destructor (004FD8B0)", true, true, "");
}

// The deleting destructors return the object; nothing else can be read afterwards.
template <typename T, void* (__fastcall* Ours)(T*, void*, unsigned)>
void* DeleteCheck(const char* name, T* self, unsigned flags) {
    void* result = Ours(self, nullptr, flags);
    ei::verify::Check(name, static_cast<void*>(self), result, "flags %u", flags);
    return result;
}
void* __fastcall PresetDeleteVerify(CRenderPreset* self, void*, unsigned flags) {
    return DeleteCheck<CRenderPreset, RenderPresetDelete>("CRenderPreset deleting destructor (004FD880)", self, flags);
}
void* __fastcall BaseDeleteVerify(void* self, void*, unsigned flags) {
    return DeleteCheck<void, RenderBaseDelete>("renderer base deleting destructor (004FCD30 / 004FD900)", self, flags);
}
void* __fastcall DeviceDeleteVerify(void* self, void*, unsigned flags) {
    return DeleteCheck<void, DeviceDelete>("device object deleting destructor (004E6880)", self, flags);
}
void* __fastcall DirectDrawHolderDeleteVerify(void* self, void*, unsigned flags) {
    return DeleteCheck<void, DirectDrawHolderDelete>("DirectDraw holder deleting destructor (004EE310)", self, flags);
}
void* __fastcall HelperDeleteVerify(void* self, void*, unsigned flags) {
    return DeleteCheck<void, RenderHelperDelete>("CRenderHelper deleting destructor (004FCC20)", self, flags);
}
void* __fastcall TlLibraryDeleteVerify(void* self, void*, unsigned flags) {
    return DeleteCheck<void, TlLibraryDelete>("TL library holder deleting destructor (004F0DF0)", self, flags);
}

// ---- The transform pipeline ----

// 004F0B90: PUSH EBP (1), MOV EBP,ESP (2), PUSH -1 (2). Side by side: the original builds a second
// pipeline in a scratch block first (its library reference and its stacks' blocks are given back
// afterwards; GetTLPipeline returns the library's one object both times). Compared: the fields, the
// stacks (empty, each with its own block) and the processor's globals (without the APIC id of the
// core that happened to run CPUID).
CTransformPipeline*(__fastcall* s_pipelineConstruct)(CTransformPipeline*, void*, const char*);
ProcessorInfo ComparableProcessor() {
    ProcessorInfo info = *TheProcessorInfo();
    info.brand &= 0x00FFFFFFu;
    return info;
}
CTransformPipeline* __fastcall PipelineConstructVerify(CTransformPipeline* self, void*, const char* folder) {
    alignas(4) unsigned char scratch[sizeof(CTransformPipeline)];
    auto* theirs = reinterpret_cast<CTransformPipeline*>(scratch);
    const ProcessorInfo before = *TheProcessorInfo();
    s_pipelineConstruct(theirs, nullptr, folder);
    const ProcessorInfo expected = ComparableProcessor();
    *TheProcessorInfo() = before;

    CTransformPipeline* result = self->TransformPipelineConstruct(folder);
    const ProcessorInfo actual = ComparableProcessor();
    bool same = result == self && self->vtable_ == theirs->vtable_ && self->refs_ == theirs->refs_ &&
                std::memcmp(self->unused_08, theirs->unused_08, sizeof(self->unused_08)) == 0 &&
                self->transform_ == theirs->transform_ && self->library_ == theirs->library_ &&
                std::memcmp(&expected, &actual, sizeof(expected)) == 0;
    for (int i = 0; i < 16; ++i) {
        const MatrixStack& mine = self->stacks_[i];
        const MatrixStack& other = theirs->stacks_[i];
        same = same && mine.count == other.count && mine.matrices && other.matrices && mine.matrices != other.matrices;
    }
    ei::verify::Check("CTransformPipeline constructor (004F0B90)", true, same,
                      "\"%s\": library %p vs %p, transform %p vs %p, features %08X vs %08X", folder, theirs->library_,
                      self->library_, static_cast<void*>(theirs->transform_), static_cast<void*>(self->transform_),
                      expected.features, actual.features);

    if (theirs->library_) gameapi::FreeLibrary(static_cast<HMODULE>(theirs->library_));
    for (int i = 15; i >= 0; --i) lib::FreeAligned32(theirs->stacks_[i].matrices);
    return result;
}

MatrixStack* __fastcall MatrixStackConstructVerify(MatrixStack* self) {
    MatrixStack* result = MatrixStackConstruct(self);
    bool good = result == self && self->count == 0 && self->matrices &&
                (reinterpret_cast<std::uintptr_t>(self->matrices) & 0x1F) == 0;
    ei::verify::Check("MatrixStack constructor (004F0DB0)", true, good, "%p", static_cast<void*>(self->matrices));
    return result;
}

void __fastcall PipelineDestructVerify(CTransformPipeline* self) {
    TransformPipelineDestruct(self);
    ei::verify::Check("CTransformPipeline destructor (004F0E10)", true,
                      self->transform_ == nullptr && self->library_ == nullptr, "");
}

// ---- The driver list ----

using DriverNode = stlp::list<DirectDrawDriver>::Node;
bool InDriverList(int index) {
    DriverNode* end = *reinterpret_cast<DriverNode* const*>(0x007AEE64);
    int count = 0;
    for (DriverNode* node = end->next; node != end; node = node->next) ++count;
    return index >= 0 && index < count;
}

// 004EE6D0: PUSH ESI (1), MOV ESI,[007AEE64] (6).
DirectDrawDriver*(__fastcall* s_driverAt)(int);
DirectDrawDriver* __fastcall DriverAtVerify(int index) {
    if (!InDriverList(index)) return DriverAt(index);
    DirectDrawDriver* expected = s_driverAt(index);
    DirectDrawDriver* result = DriverAt(index);
    ei::verify::Check("DriverAt (004EE6D0)", static_cast<void*>(expected), static_cast<void*>(result), "index %d", index);
    return result;
}

} // namespace

EI_VERIFY(0x004FDB00, ReleasePresetVerify, 0, s_unused);
EI_VERIFY(0x004FDB50, PresetBlockVerify, 7, s_presetBlock);
EI_VERIFY(0x004FDBA0, PresetVerify, 7, s_preset);
EI_VERIFY(0x004FDBC0, PresetStatesVerify, 9, s_presetStates);
EI_VERIFY(0x004FDD80, BuiltInStatesVerify, 0, s_unused);
EI_VERIFY(0x004FD8B0, PresetDestructVerify, 0, s_unused);
EI_VERIFY(0x004FD880, PresetDeleteVerify, 0, s_unused);
EI_VERIFY(0x004FCD30, BaseDeleteVerify, 0, s_unused);
EI_VERIFY(0x004FD900, BaseDeleteVerify, 0, s_unused);
EI_VERIFY(0x004E6880, DeviceDeleteVerify, 0, s_unused);
EI_VERIFY(0x004EE310, DirectDrawHolderDeleteVerify, 0, s_unused);
EI_VERIFY(0x004FCC20, HelperDeleteVerify, 0, s_unused);
EI_VERIFY(0x004F0DF0, TlLibraryDeleteVerify, 0, s_unused);
EI_VERIFY(0x004F0DB0, MatrixStackConstructVerify, 0, s_unused);
EI_VERIFY(0x004F0E10, PipelineDestructVerify, 0, s_unused);
EI_VERIFY(0x004F0B90, PipelineConstructVerify, 5, s_pipelineConstruct);
EI_VERIFY(0x004EE6D0, DriverAtVerify, 7, s_driverAt);
EI_VERIFY(0x004FCE90, StateListVerify, 5, s_stateList);
EI_VERIFY(0x004FD2A0, RecordVerify, 6, s_record);
EI_VERIFY(0x004FD9D0, ReleaseBlockVerify, 6, s_releaseBlock);
EI_VERIFY(0x004FCA60, ConstructVerify, 7, s_construct);
EI_VERIFY(0x004FCD50, DestructVerify, 7, s_destruct);
