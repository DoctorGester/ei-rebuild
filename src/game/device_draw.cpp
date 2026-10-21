// The device's indexed draw with the current material (004EB550, thiscall on the device holder,
// RET 0x1C): used by the mesh draws (004E9010) and the flat geometry draw (004EA4A0). Without a
// material it is a plain DrawIndexedPrimitive. A material (CTextureDevice::material_) brings its
// blend, a preset of state blocks (one per pass) and texture stages: with one pass the stages are
// set and drawn once; with more, the first pass sets every stage, each further pass applies its
// state block, sets stage 0 from the next stage record and draws again, and the blend states are
// restored at the end. 004EB370 (RET 0x24, used by 004E9A50) draws twice: with a first surface,
// then blended (source alpha, inverse source alpha) with a second one. See docs/notes/models.md
// ("Material draw").
#include "device.h"
#include "device_material.h"
#include "engine_object.h"
#include "model.h"   // CRenderPreset
#include "texture.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render_state.h"
#include "render/render.h"
#include "originals_render.h"
#include "game_singletons.h"

#include <cstdint>

namespace {

CDeviceMaterial* CurrentMaterial(CTextureDevice* device) { return device->material_; }
int PassCount(const CRenderPreset* preset) { return static_cast<int>(preset->blocks_.size()); }
CDXSurface* TextureSurface(CEngineObject* texture) { return static_cast<CTexture*>(texture)->surface_; }

void SetRenderState(CTextureDevice* device, D3DRENDERSTATETYPE state, std::uint32_t value) {
    device::SetRenderState(device::D3D(device), state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}

// *slot = texture with counted references, the old one released through its slot 4 [004EBE60].
void AssignTexture(CEngineObject** slot, CEngineObject* value) {
    CEngineObject* previous = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (previous) previous->Release();
}
// The device's batched vertices flushed [004E7840, CTextureDevice::FlushBatch].
void FlushBatch(CTextureDevice* device) { device->FlushBatch(); }

struct DrawArgs {
    int primitive;
    int fvf;
    void* vertices;
    int vertexCount;
    const std::uint16_t* indices;
    unsigned indexCount;
    int flags;
};

// DrawIndexedPrimitive under the device lock (007AEE78), the result in lastLoadResult_, and the
// vertex, triangle and call counters. A mesh handed over to the new renderer (src/render,
// device_mesh.cpp) gets this pass with the device's current state; the old draw is skipped when the
// old path no longer draws meshes.
void Draw(CTextureDevice* device, const DrawArgs& a, unsigned triangles) {
    if (render::MeshPass(device)) {
        gameapi::EnterCriticalSection(TheDeviceLock());
        device->lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(device), 
            static_cast<D3DPRIMITIVETYPE>(a.primitive), static_cast<DWORD>(a.fvf), a.vertices,
            static_cast<DWORD>(a.vertexCount), const_cast<WORD*>(a.indices), a.indexCount, static_cast<DWORD>(a.flags));
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    device->vertexCount_ += a.vertexCount;
    device->primitiveCount_ += static_cast<int>(triangles);
    device->drawCount_ += 1;
}

// A generated coordinate index (above 0xFFFF) also sets the stage's transform flags to two
// coordinates, before or after the index.
void SetTexCoordIndex(CTextureDevice* device, int stage, std::uint32_t index, bool flagsFirst) {
    if (flagsFirst && index > 0xFFFF)
        device::SetTextureStageState(device::D3D(device), static_cast<DWORD>(stage), D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    device::SetTextureStageState(device::D3D(device), static_cast<DWORD>(stage), D3DTSS_TEXCOORDINDEX, index);
    if (!flagsFirst && index > 0xFFFF)
        device::SetTextureStageState(device::D3D(device), static_cast<DWORD>(stage), D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    if (index > 0xFFFF) render::state::Stage(static_cast<std::uint32_t>(stage), D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    render::state::Stage(static_cast<std::uint32_t>(stage), D3DTSS_TEXCOORDINDEX, index);
}

// Single pass: the stage's texture, looked up by name on first use (an inline counted assignment);
// a stage without one unbinds the device's texture of that stage (stageSurfaces_) once.
void SetSinglePassStage(CTextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
        return;
    }
    if (!s.name.empty()) {
        CTexture* texture = TheTextureManager()->FindTexture(s.name.data());
        CEngineObject* old = s.texture;
        s.texture = texture;
        if (texture) ++texture->refCount_;
        if (old) old->Release();
        device->BindSurface(stage, s.texture ? TextureSurface(s.texture) : nullptr);
        return;
    }
    CDXSurface*& bound = device->stageSurfaces_[stage];
    if (bound) {
        bound = nullptr;
        FlushBatch(device);
        device->lastLoadResult_ = device::SetTexture(device::D3D(device), static_cast<DWORD>(stage), nullptr);
        ++device->textureChanges_;
    }
}

// First of several passes: every stage of the first pass.
void SetFirstPassStage(CTextureDevice* device, int stage, MaterialStage& s) {
    if (s.texture) {
        device->BindSurface(stage, TextureSurface(s.texture));
    } else if (!s.name.empty()) {
        AssignTexture(&s.texture, TheTextureManager()->FindTexture(s.name.data()));
        device->BindTexture(stage, static_cast<CTexture*>(s.texture));
    } else {
        device->BindSurface(stage, nullptr);
    }
}

// A further pass: its stage record on stage 0.
void SetLaterPassStage(CTextureDevice* device, MaterialStage& s) {
    CDXSurface* surface = nullptr;
    if (s.texture) {
        surface = TextureSurface(s.texture);
    } else if (!s.name.empty()) {
        AssignTexture(&s.texture, TheTextureManager()->FindTexture(s.name.data()));
        if (s.texture) surface = TextureSurface(s.texture);
    }
    device->BindSurface(0, surface);
}

void DrawPasses(CTextureDevice* device, CRenderPreset* preset, const DrawArgs& args) {
    DWORD savedBlend;
    DWORD savedSource;
    DWORD savedDest;
    device::GetRenderState(device::D3D(device), D3DRENDERSTATE_ALPHABLENDENABLE, &savedBlend);
    device::GetRenderState(device::D3D(device), D3DRENDERSTATE_SRCBLEND, &savedSource);
    device::GetRenderState(device::D3D(device), D3DRENDERSTATE_DESTBLEND, &savedDest);
    SetRenderState(device, D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    SetRenderState(device, D3DRENDERSTATE_SRCBLEND, static_cast<std::uint32_t>(CurrentMaterial(device)->sourceBlend));
    SetRenderState(device, D3DRENDERSTATE_DESTBLEND, static_cast<std::uint32_t>(CurrentMaterial(device)->destBlend));
    device::ApplyStateBlock(device::D3D(device), preset->blocks_[0]);
    render::state::Block(preset->blocks_[0]);
    for (int stage = 0; stage < preset->stages_; ++stage) {
        MaterialStage& s = CurrentMaterial(device)->stages[stage];
        SetTexCoordIndex(device, stage, s.texCoordIndex, true);
        SetFirstPassStage(device, stage, s);
    }
    unsigned triangles = args.indexCount / 3;
    Draw(device, args, triangles);
    // The original tests the byte size rounded down to dwords: more than one block.
    if (((reinterpret_cast<char*>(preset->blocks_.end()) - reinterpret_cast<char*>(preset->blocks_.begin())) & ~3) > 4) {
        int pass = 1;
        do {
            device::ApplyStateBlock(device::D3D(device), preset->blocks_[pass]);
            render::state::Block(preset->blocks_[pass]);
            MaterialStage& s = CurrentMaterial(device)->stages[preset->stages_ + pass - 1];
            SetTexCoordIndex(device, 0, s.texCoordIndex, true);
            SetLaterPassStage(device, s);
            Draw(device, args, triangles);
            ++pass;
        } while (pass < PassCount(preset));
    }
    SetRenderState(device, D3DRENDERSTATE_ALPHABLENDENABLE, savedBlend);
    SetRenderState(device, D3DRENDERSTATE_SRCBLEND, savedSource);
    SetRenderState(device, D3DRENDERSTATE_DESTBLEND, savedDest);
}

// Stage 0's surface, as 004EB300 binds it (inline in 004EB370): when it changes, the batched
// vertices are flushed, the stage's texture set (null: no holder has a DirectDraw surface; the new
// renderer draws with the holder in stageSurfaces_), and the change counted.
void BindStage0(CTextureDevice* device, CDXSurface* holder) {
    if (device->stageSurfaces_[0] == holder) return;
    device->stageSurfaces_[0] = holder;
    FlushBatch(device);
    device->lastLoadResult_ = device::SetTexture(device::D3D(device), 0, nullptr);
    ++device->textureChanges_;
}

} // namespace

// 004EB550. Draws indexed primitives (DrawIndexedPrimitive's arguments) with the current material.
// Returns 1.
int CTextureDevice::DrawIndexed(int primitive, int fvf, void* vertices, int vertexCount,
                                 const std::uint16_t* indices, unsigned indexCount, int flags) {
    DrawArgs args = {primitive, fvf, vertices, vertexCount, indices, indexCount, flags};
    CDeviceMaterial* material = CurrentMaterial(this);
    if (material) {
        CRenderPreset* preset = material->preset;
        if (material->blend) {
            SetRenderState(this, D3DRENDERSTATE_ALPHABLENDENABLE, 1);
            SetRenderState(this, D3DRENDERSTATE_SRCBLEND, static_cast<std::uint32_t>(CurrentMaterial(this)->sourceBlend));
            SetRenderState(this, D3DRENDERSTATE_DESTBLEND, static_cast<std::uint32_t>(CurrentMaterial(this)->destBlend));
        } else if (static_cast<unsigned>(PassCount(preset)) <= 1) {
            SetRenderState(this, D3DRENDERSTATE_ALPHABLENDENABLE, 0);
        }
        if (static_cast<unsigned>(PassCount(preset)) > 1) {
            DrawPasses(this, preset, args);
            return 1;
        }
        device::ApplyStateBlock(device::D3D(this), preset->blocks_[0]);
        render::state::Block(preset->blocks_[0]);
        for (int stage = 0; stage < preset->stages_; ++stage) {
            SetTexCoordIndex(this, stage, CurrentMaterial(this)->stages[stage].texCoordIndex, false);
            SetSinglePassStage(this, stage, CurrentMaterial(this)->stages[stage]);
        }
    }
    Draw(this, args, indexCount / 3);
    return 1;
}
EI_REPLACE_METHOD(0x004EB550, CTextureDevice::DrawIndexed);

// 004EB370. Draws the indexed primitives with `first` on stage 0, then again blended (source
// alpha / inverse source alpha, blending on) with `second` and texture coordinates 0. Returns 1.
int CTextureDevice::DrawTwoTextures(CDXSurface* first, CDXSurface* second, int primitive,
                                     int fvf, void* vertices, int vertexCount, const std::uint16_t* indices,
                                     unsigned indexCount, int flags) {
    DrawArgs args = {primitive, fvf, vertices, vertexCount, indices, indexCount, flags};
    BindStage0(this, first);
    unsigned triangles = indexCount / 3;
    Draw(this, args, triangles);
    SetRenderState(this, D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    SetRenderState(this, D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    SetRenderState(this, D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    BindStage0(this, second);
    lastLoadResult_ = device::SetTextureStageState(device::D3D(this), 0, D3DTSS_TEXCOORDINDEX, 0);
    render::state::Stage(0, D3DTSS_TEXCOORDINDEX, 0);
    Draw(this, args, triangles);
    return 1;
}
EI_REPLACE_METHOD(0x004EB370, CTextureDevice::DrawTwoTextures);
