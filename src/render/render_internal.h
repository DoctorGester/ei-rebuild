#pragma once

// Internals shared by the new renderer's files (render.cpp: device, frame, targets, window;
// render_terrain.cpp: terrain; render_textures.cpp: game textures as sokol images).

#include "sokol_gfx.h"

#include <cstdint>
#include <vector>

namespace render::detail {

// A 4x4 matrix as the game's transform pipeline keeps it: row-major, out = M * (x, y, z, 1), the
// rows giving clip x, y, z and w (Direct3D clip space: -w <= x, y <= w, 0 <= z <= w).
struct Matrix {
    float m[16];
};

// What a frame draws, collected while the game runs its draw code and drawn by EndFrame.
struct TerrainVertex {
    float x, y, z;
    std::uint32_t colour;     // D3DCOLOR (bytes B, G, R, A)
    std::uint32_t specular;   // D3DCOLOR; its alpha is the fog factor (255 no fog)
    float u, v;
};
static_assert(sizeof(TerrainVertex) == 28);

struct TerrainBatch {
    const void* texture;      // the game's CTexture
    bool water;               // the water pass (blended over what is drawn)
    int matrix;               // index into Frame::matrices
    int firstIndex;
    int indexCount;
};

struct Frame {
    std::vector<Matrix> matrices;
    std::vector<TerrainVertex> terrainVertices;
    std::vector<std::uint32_t> terrainIndices;
    std::vector<TerrainBatch> terrainBatches;
    bool haveFog = false;
    bool fogEnabled = false;
    std::uint32_t fogColour = 0;   // D3DCOLOR
};
Frame& CurrentFrame();

// Index of `m` in the frame's matrices (added when it differs from the last one).
int AddMatrix(const float* m);

// The terrain pipelines, drawn inside EndFrame's off-screen pass (render_terrain.cpp).
bool CreateTerrainPipelines();
void DrawTerrain(const Frame& frame);
void DestroyTerrain();

// The game texture `texture` (a CTexture) as a texture view for sampling; a white texture when it
// cannot be read (render_textures.cpp). Valid until DestroyTextures.
sg_view TextureView(const void* texture);
sg_sampler LinearSampler();
void DestroyTextures();

// The game's device lock (007AEE78), held around DirectDraw calls on the game's surfaces.
void LockGameDevice();
void UnlockGameDevice();

// Pixel formats of the off-screen pass (render.cpp).
constexpr sg_pixel_format kColourFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;

} // namespace render::detail
