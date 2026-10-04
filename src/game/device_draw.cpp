// The device's indexed draw with the current material (004EB550, thiscall on the device holder,
// RET 0x1C): used by the mesh draws (004E9010) and the flat geometry draw (004EA4A0). Without a
// material it is a plain DrawIndexedPrimitive. A material (+0x14C) brings its blend, a technique of
// state blocks (one per pass) and texture stages: with one pass the stages are set and drawn once;
// with more, the first pass sets every stage, each further pass applies its state block, sets
// stage 0 from the next stage record and draws again, and the blend states are restored at the
// end. 004EB370 (RET 0x24, used by 004E9A50) draws twice: with a first surface, then blended
// (source alpha, inverse source alpha) with a second one. See docs/notes/models.md ("Material draw").
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render_state.h"
#include "render/render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// A texture stage of a material (0x14 bytes).
struct MaterialStage {
    std::uint32_t texCoordIndex;    // +0x00 D3DTSS_TEXCOORDINDEX; above 0xFFFF a generated one
    const char* nameBegin;          // +0x04 STLport string: the texture name
    const char* nameEnd;            // +0x08
    const char* nameCapacity;       // +0x0C
    void* texture;                  // +0x10 CTexture (counted), looked up by name on first use
};
static_assert(sizeof(MaterialStage) == 0x14);

// The passes: a state block handle each.
struct Technique {
    std::uint8_t unknown_00[8];
    std::uint32_t* blocksBegin;     // +0x08 vector<state block handle>
    std::uint32_t* blocksEnd;       // +0x0C
    std::uint8_t unknown_10[4];
    int stageCount;                 // +0x14 stages of the first pass
};

struct Material {
    std::uint8_t unknown_00[8];
    std::uint8_t blend;             // +0x08 alpha blending on
    std::uint8_t unknown_09[3];
    std::uint32_t sourceBlend;      // +0x0C
    std::uint32_t destBlend;        // +0x10
    Technique* technique;           // +0x14
    MaterialStage* stages;          // +0x18
};

Material* CurrentMaterial(char* device) { return At<Material*>(device, 0x14C); }
void* Direct3D(char* device) { return At<void*>(device, 0x54); }
int PassCount(const Technique* t) { return static_cast<int>(t->blocksEnd - t->blocksBegin); }
void* TextureSurface(void* texture) { return At<void*>(texture, 0x5C); }

// IDirect3DDevice7 slots.
constexpr int kSetRenderState = 20;
constexpr int kGetRenderState = 21;
constexpr int kDrawIndexedPrimitive = 26;
constexpr int kSetTexture = 35;
constexpr int kSetTextureStageState = 37;
constexpr int kApplyStateBlock = 39;
constexpr int kAlphaBlendEnable = 0x1B;
constexpr int kSourceBlend = 0x13;
constexpr int kDestBlend = 0x14;
constexpr int kTexCoordIndex = 0xB;
constexpr int kTextureTransformFlags = 0x18;
constexpr int kCount2 = 2;
template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}
void SetRenderState(char* device, int state, std::uint32_t value) {
    ComCall(Direct3D(device), kSetRenderState, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}

// The texture manager (007AF744): finds or loads a texture by name [00693C60].
void* TextureManager() { return *reinterpret_cast<void**>(0x007AF744); }
__declspec(naked) void* __fastcall FindTexture(void*, void*, const char*) EI_ORIGINAL(0x00693C60)
// *slot = texture with counted references, the old one released [004EBE60].
__declspec(naked) void __fastcall AssignTexture(void**, void*, void*) EI_ORIGINAL(0x004EBE60)
// The device [thiscall]: binds a surface to a stage [004EB300: stage, surface], binds a texture to
// a stage [004EBAF0: stage, texture], flushes the batched vertices [004E7840].
__declspec(naked) void __fastcall BindSurface(char*, void*, int, void*) EI_ORIGINAL(0x004EB300)
__declspec(naked) void __fastcall BindTexture(char*, void*, int, void*) EI_ORIGINAL(0x004EBAF0)
__declspec(naked) void __fastcall FlushBatch(char*) EI_ORIGINAL(0x004E7840)

struct DrawArgs {
    int primitive;
    int fvf;
    void* vertices;
    int vertexCount;
    const std::uint16_t* indices;
    unsigned indexCount;
    int flags;
};

// DrawIndexedPrimitive under the device lock (007AEE78), the result in +8, and the vertex,
// triangle and call counters (+0x16C..+0x174). A mesh handed over to the new renderer (src/render,
// device_mesh.cpp) gets this pass with the device's current state; the old draw is skipped when the
// old path no longer draws meshes.
void Draw(char* device, const DrawArgs& a, unsigned triangles) {
    if (render::MeshPass(device)) {
        CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
        gameapi::EnterCriticalSection(lock);
        At<long>(device, 8) = ComCall(Direct3D(device), kDrawIndexedPrimitive, a.primitive, a.fvf, a.vertices,
                                      a.vertexCount, a.indices, a.indexCount, a.flags);
        gameapi::LeaveCriticalSection(lock);
    }
    At<int>(device, 0x16C) += a.vertexCount;
    At<unsigned>(device, 0x170) += triangles;
    At<int>(device, 0x174) += 1;
}

void SetTexCoordIndex(char* device, int stage, std::uint32_t index, bool flagsFirst) {
    if (flagsFirst && index > 0xFFFF) ComCall(Direct3D(device), kSetTextureStageState, stage, kTextureTransformFlags, kCount2);
    ComCall(Direct3D(device), kSetTextureStageState, stage, kTexCoordIndex, index);
    if (!flagsFirst && index > 0xFFFF) ComCall(Direct3D(device), kSetTextureStageState, stage, kTextureTransformFlags, kCount2);
    if (index > 0xFFFF) render::state::Stage(static_cast<std::uint32_t>(stage), kTextureTransformFlags, kCount2);
    render::state::Stage(static_cast<std::uint32_t>(stage), kTexCoordIndex, index);
}

// Single pass: the stage's texture, looked up by name on first use (an inline counted assignment);
// a stage without one unbinds the device's texture of that stage (+0x12C) once.
void SetSinglePassStage(char* device, int stage, MaterialStage& s) {
    if (s.texture) {
        BindSurface(device, nullptr, stage, TextureSurface(s.texture));
        return;
    }
    if (s.nameBegin != s.nameEnd) {
        void* texture = FindTexture(TextureManager(), nullptr, s.nameBegin);
        void* old = s.texture;
        s.texture = texture;
        if (texture) ++At<int>(texture, 4);
        if (old) reinterpret_cast<void(__fastcall*)(void*, void*)>((*static_cast<void***>(old))[4])(old, nullptr);
        BindSurface(device, nullptr, stage, s.texture ? TextureSurface(s.texture) : nullptr);
        return;
    }
    void*& bound = At<void*>(device, 0x12C + stage * 4);
    if (bound) {
        bound = nullptr;
        FlushBatch(device);
        At<long>(device, 8) = ComCall(Direct3D(device), kSetTexture, stage, static_cast<void*>(nullptr));
        ++At<int>(device, 0x178);
    }
}

// First of several passes: every stage of the first pass.
void SetFirstPassStage(char* device, int stage, MaterialStage& s) {
    if (s.texture) {
        BindSurface(device, nullptr, stage, TextureSurface(s.texture));
    } else if (s.nameBegin != s.nameEnd) {
        AssignTexture(&s.texture, nullptr, FindTexture(TextureManager(), nullptr, s.nameBegin));
        BindTexture(device, nullptr, stage, s.texture);
    } else {
        BindSurface(device, nullptr, stage, nullptr);
    }
}

// A further pass: its stage record on stage 0.
void SetLaterPassStage(char* device, MaterialStage& s) {
    void* surface = nullptr;
    if (s.texture) {
        surface = TextureSurface(s.texture);
    } else if (s.nameBegin != s.nameEnd) {
        AssignTexture(&s.texture, nullptr, FindTexture(TextureManager(), nullptr, s.nameBegin));
        if (s.texture) surface = TextureSurface(s.texture);
    }
    BindSurface(device, nullptr, 0, surface);
}

void DrawPasses(char* device, Technique* technique, const DrawArgs& args) {
    std::uint32_t savedBlend;
    std::uint32_t savedSource;
    std::uint32_t savedDest;
    ComCall(Direct3D(device), kGetRenderState, kAlphaBlendEnable, &savedBlend);
    ComCall(Direct3D(device), kGetRenderState, kSourceBlend, &savedSource);
    ComCall(Direct3D(device), kGetRenderState, kDestBlend, &savedDest);
    SetRenderState(device, kAlphaBlendEnable, 1);
    SetRenderState(device, kSourceBlend, CurrentMaterial(device)->sourceBlend);
    SetRenderState(device, kDestBlend, CurrentMaterial(device)->destBlend);
    ComCall(Direct3D(device), kApplyStateBlock, technique->blocksBegin[0]);
    render::state::Block(technique->blocksBegin[0]);
    for (int stage = 0; stage < technique->stageCount; ++stage) {
        MaterialStage& s = CurrentMaterial(device)->stages[stage];
        SetTexCoordIndex(device, stage, s.texCoordIndex, true);
        SetFirstPassStage(device, stage, s);
    }
    unsigned triangles = args.indexCount / 3;
    Draw(device, args, triangles);
    // The original tests the byte size rounded down to dwords: more than one block.
    if (((reinterpret_cast<char*>(technique->blocksEnd) - reinterpret_cast<char*>(technique->blocksBegin)) & ~3) > 4) {
        int pass = 1;
        do {
            ComCall(Direct3D(device), kApplyStateBlock, technique->blocksBegin[pass]);
            render::state::Block(technique->blocksBegin[pass]);
            MaterialStage& s = CurrentMaterial(device)->stages[technique->stageCount + pass - 1];
            SetTexCoordIndex(device, 0, s.texCoordIndex, true);
            SetLaterPassStage(device, s);
            Draw(device, args, triangles);
            ++pass;
        } while (pass < PassCount(technique));
    }
    SetRenderState(device, kAlphaBlendEnable, savedBlend);
    SetRenderState(device, kSourceBlend, savedSource);
    SetRenderState(device, kDestBlend, savedDest);
}

// Stage 0's surface, as 004EB300 binds it (inline in 004EB370): when it changes, the batched
// vertices are flushed, the surface's DirectDraw surface (+0x10) set, and the change counted.
void BindStage0(char* device, void* surface) {
    if (At<void*>(device, 0x12C) == surface) return;
    At<void*>(device, 0x12C) = surface;
    FlushBatch(device);
    void* dd = surface ? At<void*>(surface, 0x10) : nullptr;
    At<long>(device, 8) = ComCall(Direct3D(device), kSetTexture, 0, dd);
    ++At<int>(device, 0x178);
}

} // namespace

// 004EB550. Draws indexed primitives (DrawIndexedPrimitive's arguments) with the current material.
// Returns 1.
int __fastcall DeviceDrawIndexed(char* device, void*, int primitive, int fvf, void* vertices, int vertexCount,
                                 const std::uint16_t* indices, unsigned indexCount, int flags) {
    DrawArgs args = {primitive, fvf, vertices, vertexCount, indices, indexCount, flags};
    Material* material = CurrentMaterial(device);
    if (material) {
        Technique* technique = material->technique;
        if (material->blend) {
            SetRenderState(device, kAlphaBlendEnable, 1);
            SetRenderState(device, kSourceBlend, CurrentMaterial(device)->sourceBlend);
            SetRenderState(device, kDestBlend, CurrentMaterial(device)->destBlend);
        } else if (static_cast<unsigned>(PassCount(technique)) <= 1) {
            SetRenderState(device, kAlphaBlendEnable, 0);
        }
        if (static_cast<unsigned>(PassCount(technique)) > 1) {
            DrawPasses(device, technique, args);
            return 1;
        }
        ComCall(Direct3D(device), kApplyStateBlock, technique->blocksBegin[0]);
        render::state::Block(technique->blocksBegin[0]);
        for (int stage = 0; stage < technique->stageCount; ++stage) {
            SetTexCoordIndex(device, stage, CurrentMaterial(device)->stages[stage].texCoordIndex, false);
            SetSinglePassStage(device, stage, CurrentMaterial(device)->stages[stage]);
        }
    }
    Draw(device, args, indexCount / 3);
    return 1;
}
EI_REPLACE(0x004EB550, DeviceDrawIndexed);

// 004EB370. Draws the indexed primitives with `first` on stage 0, then again blended (source
// alpha / inverse source alpha, blending on) with `second` and texture coordinates 0. Returns 1.
int __fastcall DeviceDrawTwoTextures(char* device, void*, void* first, void* second, int primitive, int fvf, void* vertices,
                                     int vertexCount, const std::uint16_t* indices, unsigned indexCount, int flags) {
    DrawArgs args = {primitive, fvf, vertices, vertexCount, indices, indexCount, flags};
    BindStage0(device, first);
    unsigned triangles = indexCount / 3;
    Draw(device, args, triangles);
    SetRenderState(device, kSourceBlend, 5);
    SetRenderState(device, kDestBlend, 6);
    SetRenderState(device, kAlphaBlendEnable, 1);
    BindStage0(device, second);
    At<long>(device, 8) = ComCall(Direct3D(device), kSetTextureStageState, 0, kTexCoordIndex, 0);
    render::state::Stage(0, kTexCoordIndex, 0);
    Draw(device, args, triangles);
    return 1;
}
EI_REPLACE(0x004EB370, DeviceDrawTwoTextures);
