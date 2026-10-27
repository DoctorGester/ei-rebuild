#pragma once

// The material the old renderer's device draws with (CTextureDevice::material_): set in device_states.cpp,
// applied by the indexed draw (device_draw.cpp). See docs/notes/models.md ("Material draw").

#include "engine_object.h"
#include "texture.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CRenderPreset;   // model.h

// A texture stage of a material (0x14 bytes): copied with the texture gaining a reference, destroyed by releasing
// the texture and freeing the name.
struct MaterialStage {
    explicit MaterialStage(CTexture* stageTexture) : name("", ""), texture(stageTexture) {
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
    CTexture* texture;              // +0x10 counted, looked up by name on first use
};
static_assert(sizeof(MaterialStage) == 0x14 && offsetof(MaterialStage, texture) == 0x10);

// A device material (counted; deleted at its last reference). Made by the game's code (the wounds material,
// WoundsMaterial in map_object_shadow.h, is the one kind); this is the part the device reads.
class CDeviceMaterial {
public:
    // Drops a reference; deleted at zero.
    void Release() {
        if (--refCount == 0) delete this;
    }

    // On the game's heap (device_states.cpp).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CDeviceMaterial(const CDeviceMaterial&) = delete;
    CDeviceMaterial& operator=(const CDeviceMaterial&) = delete;

    int refCount = 0;               // +0x04
    std::uint8_t blend;             // +0x08 alpha blending on (with sourceBlend / destBlend)
    int sourceBlend;                // +0x0C D3DBLEND
    int destBlend;                  // +0x10 D3DBLEND
    // +0x14 the passes (counted): a Direct3D state block each, the first pass's texture stages.
    CRenderPreset* preset;
    stlp::vector<MaterialStage> stages;   // +0x18 the first pass's stages, then one per further pass

protected:
    CDeviceMaterial() {}
    virtual ~CDeviceMaterial() = default;
};
