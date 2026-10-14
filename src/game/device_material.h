#pragma once

// The material the old renderer's device draws with (CTextureDevice::material_): set by 004EB170
// (device_states.cpp), applied by the indexed draw 004EB550 (device_draw.cpp). See
// docs/notes/models.md ("Material draw").

#include "engine_object.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CRenderPreset;   // model.h

// A texture stage of a material (0x14 bytes): copied by 0051E970 / 0051E430 (the texture gains a
// reference), destroyed by releasing the texture (0051E3D0) and freeing the name.
struct MaterialStage {
    explicit MaterialStage(CEngineObject* stageTexture) : name("", ""), texture(stageTexture) {
        if (texture) ++texture->refCount_;
    }
    MaterialStage(const MaterialStage& other)
        : texCoordIndex(other.texCoordIndex), name(other.name), texture(other.texture) {
        if (texture) ++texture->refCount_;
    }
    MaterialStage& operator=(const MaterialStage&) = delete;
    ~MaterialStage() {
        if (texture) texture->Release();
    }

    // +0x00 the stage's D3DTSS_TEXCOORDINDEX; above 0xFFFF a generated one (the draw then also sets
    // D3DTSS_TEXTURETRANSFORMFLAGS to D3DTTFF_COUNT2).
    std::uint32_t texCoordIndex = 0;
    stlp::string name;              // +0x04 the texture's name (empty for the wounds stages)
    CEngineObject* texture;         // +0x10 a CTexture (counted), looked up by name on first use
};
static_assert(sizeof(MaterialStage) == 0x14 && offsetof(MaterialStage, texture) == 0x10);

// A device material (counted; vtable slot 0 the deleting destructor; the root vtable 0073C368).
// Made by the game's code (the wounds material, WoundsMaterial in map_object_shadow.h, is one);
// only the layout the device reads is declared.
class __declspec(novtable) CDeviceMaterial {
public:
    virtual void DeletingDestructor(unsigned flags);   // 0

    // Drops a reference; the deleting destructor (slot 0, with 1) at zero.
    void Release() {
        if (--refCount == 0) DeletingDestructor(1);
    }

    int refCount;                   // +0x04
    std::uint8_t blend;             // +0x08 alpha blending on (with sourceBlend / destBlend)
    int sourceBlend;                // +0x0C D3DBLEND
    int destBlend;                  // +0x10 D3DBLEND
    // +0x14 the passes (counted): a Direct3D state block each, the first pass's texture stages.
    CRenderPreset* preset;
    stlp::vector<MaterialStage> stages;   // +0x18 the first pass's stages, then one per further pass
};
static_assert(offsetof(CDeviceMaterial, refCount) == 0x04 && offsetof(CDeviceMaterial, blend) == 0x08 &&
              offsetof(CDeviceMaterial, sourceBlend) == 0x0C && offsetof(CDeviceMaterial, destBlend) == 0x10);
static_assert(offsetof(CDeviceMaterial, preset) == 0x14 && offsetof(CDeviceMaterial, stages) == 0x18 &&
              sizeof(CDeviceMaterial) == 0x24);
