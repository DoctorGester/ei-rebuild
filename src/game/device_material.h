#pragma once

// The material the old renderer's device draws with (CTextureDevice::material_): set in device_states.cpp,
// applied by the indexed draw. See docs/notes/models.md ("Material draw").

#include "engine_object.h"
#include "texture.h"


#include <cstddef>
#include <cstdint>
#include <vector>

class CRenderPreset;

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

    // the stage's D3DTSS_TEXCOORDINDEX; above 0xFFFF a generated one (the draw then also sets
    // D3DTSS_TEXTURETRANSFORMFLAGS to D3DTTFF_COUNT2).
    std::uint32_t texCoordIndex = 0;
    std::string name;               // the texture's name (empty for the wounds stages)
    CTexture* texture;              // counted, looked up by name on first use
};

// A device material (counted; deleted at its last reference). Made by the game's code (the wounds material,
// WoundsMaterial in map_object_shadow.h, is the one kind); this is the part the device reads.
class CDeviceMaterial {
public:
    void Release() {
        if (--refCount == 0) delete this;
    }

    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CDeviceMaterial(const CDeviceMaterial&) = delete;
    CDeviceMaterial& operator=(const CDeviceMaterial&) = delete;

    int refCount = 0;
    std::uint8_t blend;             // alpha blending on (with sourceBlend / destBlend)
    int sourceBlend;                // D3DBLEND
    int destBlend;                  // D3DBLEND
    // the passes (counted): a Direct3D state block each, the first pass's texture stages.
    CRenderPreset* preset;
    std::vector<MaterialStage> stages;   // the first pass's stages, then one per further pass

protected:
    CDeviceMaterial() {}
    virtual ~CDeviceMaterial() = default;
};
