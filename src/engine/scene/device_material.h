#pragma once

// The material the device draws with (TextureDevice::material): set in device_draw.cpp,
// applied by the indexed draw. See docs/notes/models.md ("Material draw").

#include "engine/core/engine_object.h"
#include "engine/core/ref.h"
#include "engine/scene/render_helper.h"   // RenderPreset
#include "engine/models/texture.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A texture stage of a material: copied with the texture gaining a reference, destroyed by releasing the texture and
// freeing the name.
struct MaterialStage {
    explicit MaterialStage(Texture* stageTexture) : name("", ""), texture(stageTexture) {}
    MaterialStage(const MaterialStage&) = default;
    MaterialStage& operator=(const MaterialStage&) = delete;

    std::string name;               // the texture's name (empty for the wounds stages)
    HeldRef<Texture> texture;      // looked up by name on first use
};

// A device material (counted; deleted at its last reference). Made by the game's code (the wounds material,
// WoundsMaterial in map_object_shadow.h, is the one kind); this is the part the device reads.
class DeviceMaterial {
public:
    void AddRef() { ++refCount; }
    void Release() {
        if (--refCount == 0) delete this;
    }


    DeviceMaterial(const DeviceMaterial&) = delete;
    DeviceMaterial& operator=(const DeviceMaterial&) = delete;

    int refCount = 0;
    std::uint8_t blend = 0;         // alpha blending on (with sourceBlend / destBlend)
    render::Blend sourceBlend = {};  // the factors the draw blends with
    render::Blend destBlend = {};
    // the passes (counted): the renderer helper's preset, the first pass's texture stages.
    Ref<RenderPreset> preset;
    std::vector<MaterialStage> stages;   // the first pass's stages, then one per further pass

protected:
    DeviceMaterial() {}
    virtual ~DeviceMaterial() = default;
};
