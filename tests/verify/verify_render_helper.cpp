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
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/lib/stlp.h"

#include <cstdint>
#include <cstring>

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

namespace {
void* s_unused;

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
EI_VERIFY(0x004EE6D0, DriverAtVerify, 7, s_driverAt);
