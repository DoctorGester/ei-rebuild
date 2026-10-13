// The renderer's helper object (CRenderHelper, model.h; vtable 0073D840: the Direct3D state blocks
// behind the material presets) and the old renderer's other scalar deleting destructors (A-S11).
// Rewritten here: the helper's slots that only read its tables (2, 4, 5, 7), its preset's
// destructor, the Direct3D driver list's lookup by index and the "destructor body, then operator
// delete" shells of five classes. The bodies that talk to DirectDraw / Direct3D (the device
// object's, the DirectDraw holder's and the helper's destructors) and the other slots stay in the
// exe. See docs/notes/renderer.md (section 22).
#include "model.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// Destructor bodies not rewritten (thiscall, plain RET): the device object's 004E68A0, the
// DirectDraw holder's 004EE330 (vtable 0073D770), the helper's 004FCD50 (Direct3D state blocks and
// surfaces released), the TL library holder's 004F0E10 (vtable 0073D778: FreeLibrary).
__declspec(naked) void __fastcall DeviceDestructOriginal(void*) EI_ORIGINAL(0x004E68A0)
__declspec(naked) void __fastcall DirectDrawHolderDestructOriginal(void*) EI_ORIGINAL(0x004EE330)
__declspec(naked) void __fastcall HelperDestructOriginal(void*) EI_ORIGINAL(0x004FCD50)
__declspec(naked) void __fastcall TlLibraryDestructOriginal(void*) EI_ORIGINAL(0x004F0E10)

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

// ---- The helper's table slots -----------------------------------------------------------------------

// 004FDB00 (slot 2, RET 4).
void __fastcall RenderHelperReleasePreset(CRenderHelper* self, void*, CRenderPreset* preset) {
    if (auto* node = self->records_.Find(preset)) self->ReleaseBlock(node->value.block);
}
EI_REPLACE(0x004FDB00, RenderHelperReleasePreset);

// 004FDB50 (slot 4, RET 4).
void* __fastcall RenderHelperPresetBlock(CRenderHelper* self, void*, CRenderPreset* preset) {
    auto* node = self->records_.Find(preset);
    return node ? node->value.block : nullptr;
}
EI_REPLACE(0x004FDB50, RenderHelperPresetBlock);

// 004FDBA0 (slot 5, RET 4).
CRenderPreset* __fastcall RenderHelperPreset(CRenderHelper* self, void*, unsigned index) {
    return index < CRenderHelper::kPresetCount ? self->presets_[index] : nullptr;
}
EI_REPLACE(0x004FDBA0, RenderHelperPreset);

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
    self->items_.FreeStorage();
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

// 004FCC20: the helper's.
void* __fastcall RenderHelperDelete(void* self, void*, unsigned flags) {
    HelperDestructOriginal(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004FCC20, RenderHelperDelete);

// 004F0DF0: the TL library holder's.
void* __fastcall TlLibraryDelete(void* self, void*, unsigned flags) {
    TlLibraryDestructOriginal(self);
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
