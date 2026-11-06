// What the landscape's ground (terrain_ground.hlsl) and water (terrain_water.hlsl) programs share (render_terrain.cpp:
// the zone's mesh of a layer, every page's tiles). Each program names its fragment slots (SHADOW_BLOCK_SLOT ...
// SKY_IMAGE_SLOT, below) before including this, declares its vertex block `vs` (starting with TERRAIN_VS_MEMBERS) and
// its fragment block `fs` (TERRAIN_FS_MEMBERS), then includes terrain_stages.hlsli for the functions that read them.
//
// The vertex carries its page's layer of the page textures' array with its texture coordinates, the normal, the material
// colour (colour word) and, in the specular word, the edge fade (alpha); `rest` (loss, depth, otherWater or slope, extra)
// means one thing on the ground and another on the water (the programs' headers). A tile whose texture animates
// (`anim.y` frames, not 0) carries its texture coordinates relative to its page's first tile in `uv` and its first
// frame's index in the page, its frame count, its seed (its place in the map) and its rate (1 steps every time, less
// plays one cycle now and then) in `anim`: tile_uv (terrain_stages.hlsli) works its frame out from the landscape's
// tile-step count (Terrain::tileSteps); any other vertex's `uv` is final.
#include "bindings.hlsli"
#include "depth_copy.hlsli"
#include "distance_fog.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float3 uv : TEXCOORD3;      // u, v and the page's layer
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] float4 rest : TEXCOORD5;    // loss, depth, otherWater or the slope, extra (the programs' headers)
    [[vk::location(6)]] float4 anim : TEXCOORD6;    // the first frame's index in the page, the frames, the seed, the rate
};

// The vertex blocks' first members (the program adds its own after them): the clip matrix, the matrix to the shadow
// map, the half-pixel shift (render_internal.h), how far along its normal a receiver's point is looked up in the map,
// the distance fog (distance_fog.hlsli; fogDensity 0 none), the map's size and edge fade band (render::MapEdge; band 0
// none) and the animated tiles' state: the steps so far, the tiles per page row and a tile's side as a part of the page.
#define TERRAIN_VS_MEMBERS \
    row_major float4x4 matrix; \
    row_major float4x4 light; \
    float2 shift; \
    float normalOffset; \
    float fogStart; \
    float fogDensity; \
    float fogDepthPerW; \
    float mapWidth; \
    float mapHeight; \
    float edgeBand; \
    int tileSteps; \
    int tilesPerRow; \
    float tileSide;

// The fragment blocks' first members: the fog's colour and whether it is on, the surfaces' rain (the share of the
// cells a drop falls in; the rings' clock, client ticks) and the sky the water and the puddles reflect (render_water.cpp):
// the sky image's clip matrix, the dome (its centre in the landscape's space, its radius across and its height above
// the centre).
#define TERRAIN_FS_MEMBERS \
    float3 fog; \
    int fogOn; \
    float rain; \
    float rainClock; \
    row_major float4x4 skyView; \
    float3 dome; \
    float domeRadius; \
    float domeHeight;
