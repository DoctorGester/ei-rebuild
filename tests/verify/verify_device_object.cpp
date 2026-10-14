// Checks for device_object.cpp (A-S13). Main thread, at start-up (every scenario) unless noted.
//  - Side by side:
//    004ED820 (the Direct3D device enumeration callback; the constructor passes it by its address):
//      the original appends to a scratch list, ours to the real one; the two records are compared.
//    004E6EE0 (the texture format table): the original builds the format list and table, they are
//      noted, ours builds them again (the enumeration changes nothing in Direct3D).
//  - Ours only, the state left checked (the original cannot run next to ours: one Direct3D device
//    on the render target, one depth buffer attached to it):
//    004E5E50 (the constructor), 004E6C30 (the depth buffer), 004E68A0 (the destructor body; at
//    exit; it calls the base's 004EE330 directly and checks what that leaves too).
//    What the constructor and its parts leave is also compared between a run with ours and a run
//    with the originals through the device-state lines (verify_device_state.h).
#include "verify.h"

#include "../../src/game/device.h"
#include "../../src/game/model.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
void* s_unused;

void* VtableOf(const void* object) {
    void* vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

// 004ED820: PUSH -1 (2), PUSH 0071BB20 (5).
HRESULT(CALLBACK* s_deviceCallback)(char*, char*, D3DDEVICEDESC7*, void*);
HRESULT CALLBACK DeviceCallbackVerify(char* description, char* name, D3DDEVICEDESC7* desc, void* context) {
    stlp::list<Direct3DDeviceRecord> scratch;
    scratch.Construct();
    const HRESULT expected = s_deviceCallback(description, name, desc, &scratch);
    const HRESULT result = Direct3DDeviceEnumCallback(description, name, desc, context);
    const auto& devices = *static_cast<const stlp::list<Direct3DDeviceRecord>*>(context);
    bool same = result == expected && !scratch.Empty() && scratch.First()->next == scratch.End() && !devices.Empty();
    if (same) {
        const Direct3DDeviceRecord& made = scratch.First()->data;
        const Direct3DDeviceRecord& added = devices.End()->prev->data;
        same = made.name == added.name && made.description == added.description &&
               std::memcmp(&made.desc, &added.desc, sizeof(D3DDEVICEDESC7)) == 0;
    }
    ei::verify::Check("Direct3DDeviceEnumCallback (004ED820)", true, same, "device '%s' result %ld / %ld", name, expected,
                      result);
    scratch.ClearWith([](Direct3DDeviceRecord* record) { record->~Direct3DDeviceRecord(); });
    scratch.Destroy();
    return result;
}

// The texture formats and the table of their ids (each id with its format's place in the list).
struct FormatTable {
    std::vector<PixelFormat> formats;
    std::vector<std::pair<int, int>> ids;
    std::uint32_t largest;
    long result;
};
bool operator==(const FormatTable& a, const FormatTable& b) {
    return a.formats.size() == b.formats.size() &&
           std::memcmp(a.formats.data(), b.formats.data(), a.formats.size() * sizeof(PixelFormat)) == 0 && a.ids == b.ids &&
           a.largest == b.largest && a.result == b.result;
}
FormatTable Table(const CTextureDevice* device) {
    FormatTable table;
    for (auto* node = device->textureFormats_.First(); node != device->textureFormats_.End(); node = node->next)
        table.formats.push_back(node->data);
    device->formats_.ForEach([&](const auto* entry) {
        int place = 0;
        int found = -1;
        for (auto* node = device->textureFormats_.First(); node != device->textureFormats_.End(); node = node->next, ++place)
            if (&node->data == entry->value) found = place;
        table.ids.emplace_back(entry->key, found);
    });
    std::sort(table.ids.begin(), table.ids.end());
    table.largest = device->largestFormatId_;
    table.result = device->lastLoadResult_;
    return table;
}

// 004E6EE0: PUSH -1 (2), PUSH 0071B8B6 (5).
void(__fastcall* s_buildFormatTable)(CTextureDevice*);
void __fastcall BuildFormatTableVerify(CTextureDevice* self) {
    if (device::WithoutDirectDraw()) {   // the original would ask the stand-in: ours only (R9b)
        self->BuildFormatTable();
        const FormatTable table = Table(self);
        ei::verify::Check("CTextureDevice::BuildFormatTable (004E6EE0)", true, !table.formats.empty() && !table.ids.empty(),
                          "without DirectDraw (ours only): formats %u, ids %u", static_cast<unsigned>(table.formats.size()),
                          static_cast<unsigned>(table.ids.size()));
        return;
    }
    s_buildFormatTable(self);
    const FormatTable expected = Table(self);
    self->BuildFormatTable();
    const FormatTable table = Table(self);
    ei::verify::Check("CTextureDevice::BuildFormatTable (004E6EE0)", true, table == expected, "formats %u / %u, ids %u / %u",
                      static_cast<unsigned>(expected.formats.size()), static_cast<unsigned>(table.formats.size()),
                      static_cast<unsigned>(expected.ids.size()), static_cast<unsigned>(table.ids.size()));
}

void __fastcall CreateDepthBufferVerify(CTextureDevice* self, void*, int depthBits, int stencilBits) {
    self->CreateDepthBuffer(depthBits, stencilBits);
    const CDXSurface* depth = self->depth_;
    const CDXSurface* second = self->depth2_;
    const CDXSurface* target = self->renderTarget_;
    const bool made = depth && second && depth->refCount == 1 && second->refCount == 1 && depth->surface && second->surface &&
                      depth->device == self && second->device == self && depth->width == target->width &&
                      depth->height == target->height && second->width == 1 && second->height == 1 &&
                      (self->depthFormat_.words[1] & DDPF_ZBUFFER) && self->depthFormat_.words[0] == sizeof(DDPIXELFORMAT) &&
                      (stencilBits == 0 || self->depthFormat_.words[4] == static_cast<std::uint32_t>(stencilBits)) &&
                      self->lastLoadResult_ == 0;
    ei::verify::Check("CTextureDevice::CreateDepthBuffer (004E6C30)", true, made, "depth bits %d stencil bits %d", depthBits,
                      stencilBits);
}

void __fastcall DestructVerify(CTextureDevice* self) {
    self->Destruct();
    const bool left = VtableOf(self) == reinterpret_cast<void*>(0x0073D768) && !self->depth_ && !self->depth2_ &&
                      !self->pipeline_ && !self->primary_ && !self->renderTarget_ && !self->converter_ &&
                      !self->converterLibrary_;
    ei::verify::Check("CTextureDevice::Destruct (004E68A0)", true, left, "flags %X", self->flags_);
}

CTextureDevice* __fastcall ConstructVerify(CTextureDevice* self, void*, HWND window, GUID* driver, int fullScreen, int width,
                                           int height, int bitsPerPixel, int refreshRate, unsigned backBuffers, int depthBits,
                                           int stencilBits) {
    CTextureDevice* result = self->Construct(window, driver, fullScreen, width, height, bitsPerPixel, refreshRate, backBuffers,
                                             depthBits, stencilBits);
    // In the switch-over (device.h) no Direct3D object, device list, depth formats or depth surfaces.
    const bool direct3D = !device::WithoutDirectDraw();
    const bool made = direct3D ? self->d3d_ && self->depth_ && self->depth2_ && !self->devices_.Empty() && !self->depthFormats_.Empty()
                               : !self->d3d_ && !self->depth_ && !self->depth2_ && self->devices_.Empty() && self->depthFormats_.Empty();
    bool built = result == self && VtableOf(self) == reinterpret_cast<void*>(0x0073D760) && made && self->d3dDevice_ &&
                 !self->pipeline_ && !self->material_ && self->zEnable_ == 1 && self->colour_ == 0xFF000000 &&
                 self->lastLoadResult_ == 0 && !self->textureFormats_.Empty() && self->formats_.Size() != 0 &&
                 self->batch_.used.Empty() && !self->batch_.buffer && !self->batch_.indices.data && self->batch2_.used.Empty() &&
                 !self->batch2_.buffer && !self->batch2_.indices.data && self->spareBuffers_.Empty() && !self->spareBuffer_ &&
                 !self->spareArray_.data && std::memcmp(&self->deviceGuid_, &device::IidHalDevice(), sizeof(GUID)) == 0;
    for (const CDXSurface* holder : self->stageSurfaces_) built = built && !holder;
    // The four state blocks of stage 0's operations were recorded.
    const auto* blocks = reinterpret_cast<const std::uint32_t*>(0x007AEDD0);
    for (int i = 0; i < 4; ++i) built = built && blocks[i] != 0;
    ei::verify::Check("CTextureDevice::Construct (004E5E50)", true, built, "%d x %d, %d bits, depth %d, stencil %d%s", width,
                      height, bitsPerPixel, depthBits, stencilBits, direct3D ? "" : " (without Direct3D)");
    return result;
}

} // namespace

EI_VERIFY(0x004ED820, DeviceCallbackVerify, 7, s_deviceCallback);
EI_VERIFY(0x004E6EE0, BuildFormatTableVerify, 7, s_buildFormatTable);
EI_VERIFY(0x004E6C30, CreateDepthBufferVerify, 0, s_unused);
EI_VERIFY(0x004E68A0, DestructVerify, 0, s_unused);
EI_VERIFY(0x004E5E50, ConstructVerify, 0, s_unused);
