// The device object's state after start-up, logged once (A-S13; see verify_device_state.h). Only
// values that do not change from run to run are written: no addresses (a pointer is written as set
// or not, a format pointer as its place in the format list), no handles.
#include "verify_device_state.h"

#include "../../src/game/device.h"
#include "../../src/hook/log.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

std::uint32_t Hash(const void* data, std::size_t size, std::uint32_t hash = 2166136261u) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

std::string Guid(const GUID* guid) {
    if (!guid) return "none";
    char text[40];
    std::snprintf(text, sizeof(text), "%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X", guid->Data1, guid->Data2,
                  guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
                  guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return text;
}

std::string Words(const PixelFormat& format) {
    char text[100];
    std::snprintf(text, sizeof(text), "%X.%X.%X.%X.%X.%X.%X.%X", format.words[0], format.words[1], format.words[2],
                  format.words[3], format.words[4], format.words[5], format.words[6], format.words[7]);
    return text;
}

void Holder(const char* name, const CTextureDevice* device, const CDXSurface* holder) {
    if (!holder) {
        ei::Log("device-state: %s none", name);
        return;
    }
    ei::Log("device-state: %s refs %d result %08lX own device %d surface %d size %d x %d bits %d", name, holder->refCount,
            static_cast<unsigned long>(holder->result), static_cast<int>(holder->device == device),
            static_cast<int>(holder->surface != nullptr), holder->width, holder->height, holder->bitsPerPixel);
}

void Batch(const char* name, const DeviceVertexBatch& batch) {
    // (The batch's vertex memory and vertex count are not set by the constructor: not written.)
    ei::Log("device-state: %s used empty %d buffer %d indices %d size %d capacity %d step %d", name,
            static_cast<int>(batch.used.Empty()), static_cast<int>(batch.buffer != nullptr),
            static_cast<int>(batch.indices.data != nullptr), batch.indices.size, batch.indices.capacity,
            batch.indices.growBy);
}

void Formats(const char* name, const stlp::list<PixelFormat>& formats) {
    int count = 0;
    for (auto* node = formats.First(); node != formats.End(); node = node->next)
        ei::Log("device-state: %s %d: %s", name, count++, Words(node->data).c_str());
    ei::Log("device-state: %s count %d", name, count);
}

} // namespace

void DumpDeviceStateOnce(const CTextureDevice* device) {
    static bool done = false;
    if (done || !device) return;
    done = true;

    void* vtable;
    std::memcpy(&vtable, device, sizeof(vtable));
    ei::Log("device-state: vtable %p refs %d driver %s flags %X screen %ld,%ld,%ld,%ld window %d", vtable, device->refs_,
            Guid(device->driver_).c_str(), device->flags_, device->screen_.left, device->screen_.top,
            device->screen_.right, device->screen_.bottom, static_cast<int>(device->window_ != nullptr));
    ei::Log("device-state: ddraw %d clipper %d library %d converter %d d3d %d d3d device %d pipeline %d material %d",
            static_cast<int>(device->ddraw_ != nullptr), static_cast<int>(device->clipper_ != nullptr),
            static_cast<int>(device->converterLibrary_ != nullptr), static_cast<int>(device->converter_ != nullptr),
            static_cast<int>(device->d3d_ != nullptr), static_cast<int>(device->d3dDevice_ != nullptr),
            static_cast<int>(device->pipeline_ != nullptr), static_cast<int>(device->material_ != nullptr));
    ei::Log("device-state: video fourcc %08X overlay fourcc %08X overlay stretch %d holders alive %d", device->videoFourCC_,
            device->overlayFourCC_, static_cast<int>(device->overlayStretch_), device::HolderCount());

    // The display modes: their count and a hash of what tells one from another.
    int modes = 0;
    std::uint32_t modeHash = 2166136261u;
    for (auto* node = device->modes_.First(); node != device->modes_.End(); node = node->next, ++modes) {
        const DDSURFACEDESC2& mode = node->data;
        const std::uint32_t key[5] = {mode.dwFlags, mode.dwWidth, mode.dwHeight, mode.dwRefreshRate, mode.ddsCaps.dwCaps};
        modeHash = Hash(key, sizeof(key), modeHash);
        modeHash = Hash(&mode.ddpfPixelFormat, sizeof(mode.ddpfPixelFormat), modeHash);
    }
    ei::Log("device-state: display modes %d hash %08X", modes, modeHash);

    Holder("primary", device, device->primary_);
    Holder("render target", device, device->renderTarget_);
    Holder("depth", device, device->depth_);
    Holder("second depth", device, device->depth2_);
    if (device->primary_ && device->renderTarget_)
        ei::Log("device-state: render target is the primary's surface %d",
                static_cast<int>(device->primary_->surface == device->renderTarget_->surface));

    ei::Log("device-state: z enable %u device %s depth format %s largest format id %X colour %08X", device->zEnable_,
            Guid(&device->deviceGuid_).c_str(), Words(device->depthFormat_).c_str(), device->largestFormatId_,
            device->colour_);

    int devices = 0;
    for (auto* node = device->devices_.First(); node != device->devices_.End(); node = node->next, ++devices) {
        const Direct3DDeviceRecord& record = node->data;
        ei::Log("device-state: d3d device %d: '%s' '%s' %s description hash %08X", devices, record.name.data(),
                record.description.data(), Guid(&record.desc.deviceGUID).c_str(), Hash(&record.desc, sizeof(record.desc)));
    }
    ei::Log("device-state: d3d devices %d", devices);

    Formats("depth format", device->depthFormats_);
    Formats("texture format", device->textureFormats_);

    // The format table: each id with the place of its format in the texture format list.
    std::vector<std::pair<int, int>> table;
    device->formats_.ForEach([&](const auto* node) {
        int place = 0;
        int found = -1;
        for (auto* format = device->textureFormats_.First(); format != device->textureFormats_.End();
             format = format->next, ++place)
            if (&format->data == node->value) found = place;
        table.emplace_back(node->key, found);
    });
    std::sort(table.begin(), table.end());
    std::string text;
    for (const auto& [id, place] : table) {
        char entry[32];
        std::snprintf(entry, sizeof(entry), " %X:%d", static_cast<unsigned>(id), place);
        text += entry;
    }
    ei::Log("device-state: format table %u ids in %u buckets:%s", static_cast<unsigned>(device->formats_.Size()),
            static_cast<unsigned>(device->formats_.BucketCount()), text.c_str());

    Batch("batch", device->batch_);
    Batch("second batch", device->batch2_);
    ei::Log("device-state: spare buffers empty %d buffer %d array %d size %d capacity %d step %d",
            static_cast<int>(device->spareBuffers_.Empty()), static_cast<int>(device->spareBuffer_ != nullptr),
            static_cast<int>(device->spareArray_.data != nullptr), device->spareArray_.size, device->spareArray_.capacity,
            device->spareArray_.growBy);
    // The four state blocks of stage 0's operations (007AEDD0 .. 007AEDDC): made or not.
    const auto* blocks = reinterpret_cast<const std::uint32_t*>(0x007AEDD0);
    ei::Log("device-state: state blocks made %d %d %d %d", static_cast<int>(blocks[0] != 0), static_cast<int>(blocks[1] != 0),
            static_cast<int>(blocks[2] != 0), static_cast<int>(blocks[3] != 0));
}
