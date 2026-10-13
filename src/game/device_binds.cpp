// The device object's texture binds (004EB300 a surface holder, 004EBAF0 a texture, 004EB1D0 a
// texture kept by name) and its two vertex-buffer batch flushes (004E7840, 004E7A40). A-S13: the
// Direct3D 7 calls (SetTexture, IDirect3DVertexBuffer7::Unlock, DrawIndexedPrimitiveVB) are made as
// the original makes them. See docs/notes/renderer.md (section 12).
#include "device.h"

#include "ground_print.h"       // NamedTexture
#include "originals_engine.h"   // FatalDirectX

#include "ei/hook.h"
#include "lib/gameapi.h"

namespace {

// One batch's flush (the two functions differ only in the batch).
void Flush(CTextureDevice* device, DeviceVertexBatch& batch) {
    CVertexBufferHolder* buffer = batch.buffer;
    if (!buffer) return;
    buffer->result = static_cast<IDirect3DVertexBuffer7*>(buffer->buffer)->Unlock();
    gameapi::LeaveCriticalSection(device::Lock());
    if (buffer->result != 0) FatalDirectX(buffer->result, device::Text(0x0078E2D4));   // "Can't unlock vertex buffer.\n"
    const int vertexCount = batch.vertexCount;
    if (vertexCount != 0) {
        const int indexCount = batch.indices.size;
        if (indexCount != 0) {
            std::uint16_t* indices = batch.indices.data;
            CVertexBufferHolder* drawn = batch.buffer;
            gameapi::EnterCriticalSection(device::Lock());
            device->lastLoadResult_ = device::D3D(device)->DrawIndexedPrimitiveVB(
                D3DPT_TRIANGLELIST, static_cast<IDirect3DVertexBuffer7*>(drawn->buffer), 0,
                static_cast<DWORD>(vertexCount), indices, static_cast<DWORD>(indexCount), 0);
            gameapi::LeaveCriticalSection(device::Lock());
            device->vertexCount_ += vertexCount;
            device->primitiveCount_ += static_cast<int>(static_cast<unsigned>(indexCount) / 3);
            ++device->drawCount_;
        }
    }
    // The buffer goes to the end of the batch's list (one more reference), then leaves the batch.
    batch.used.PushBack(batch.buffer);
    if (batch.buffer) ++batch.buffer->refCount;
    CVertexBufferHolder* old = batch.buffer;
    batch.buffer = nullptr;
    if (old) old->Release();
    batch.vertices = nullptr;
    batch.vertexCount = 0;
    batch.indices.SetSizeDiscard(0);
}

} // namespace

void CTextureDevice::FlushBatch() { Flush(this, batch_); }
EI_REPLACE_METHOD(0x004E7840, CTextureDevice::FlushBatch);

void CTextureDevice::FlushBatch2() { Flush(this, batch2_); }
EI_REPLACE_METHOD(0x004E7A40, CTextureDevice::FlushBatch2);

int CTextureDevice::BindSurface(int stage, CDXSurface* holder) {
    if (stageSurfaces_[stage] == holder) return 1;
    stageSurfaces_[stage] = holder;
    FlushBatch();
    lastLoadResult_ = device::D3D(this)->SetTexture(static_cast<DWORD>(stage), holder ? device::Surface(holder) : nullptr);
    ++textureChanges_;
    return 1;
}
EI_REPLACE_METHOD(0x004EB300, CTextureDevice::BindSurface);

// The original repeats BindSurface's body for a texture and for none.
int CTextureDevice::BindTexture(int stage, CTexture* texture) {
    return BindSurface(stage, texture ? texture->surface_ : nullptr);
}
EI_REPLACE_METHOD(0x004EBAF0, CTextureDevice::BindTexture);

int CTextureDevice::BindNamedTexture(int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        CTexture* found = device::TextureManager()->FindTexture(named->name.data());
        CTexture* previous = named->texture;
        named->texture = found;
        if (found) ++found->refCount_;
        if (previous) previous->Release();
    }
    return BindSurface(stage, named->texture ? named->texture->surface_ : nullptr);
}
EI_REPLACE_METHOD(0x004EB1D0, CTextureDevice::BindNamedTexture);
