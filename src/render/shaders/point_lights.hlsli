// The point lights (render_lighting.cpp, PreparePointLights): every light of the frame in `pointLights`, binned by a
// grid of cells over the landscape's x and y (`lightGrid`), so a pixel loops over the lights of its cell only; and
// their shadows (render_point_shadows.cpp, PreparePointShadows): the cubes of depth faces of the few lights given a map
// this frame, a light a row of the atlas, a face a column. Included after shadow_receiver.hlsli (shadow_disc) by the
// terrain and mesh programs, which name its slots: POINT_BLOCK_SLOT, POINT_MAP_SLOT, POINT_LIGHTS_SLOT and
// POINT_GRID_SLOT.
struct PointLightParams {
    int count;            // the lights with a map
    float texels;         // a face's, a side
    float nearDistance;   // the faces' near distance
    float3 eye;           // the camera (the landscape's space)
    float4 shadows[4];    // per map: xyz the light (the landscape's space), w how much of its shadow shows (fading in
                          // and out)
    float4 depth[4];      // a face's depth for a distance d along its axis: x + y / d
    float2 gridOrigin;    // the grid's low corner (the landscape's x and y)
    int2 gridSize;        // its cells along x and y
    float gridScale;      // 1 / a cell's side
};
ConstantBuffer<PointLightParams> points : FS_BLOCK(POINT_BLOCK_SLOT);
Texture2D<float> point_shadow_map : FS_TEXTURE(POINT_MAP_SLOT);
SamplerComparisonState point_shadow_smp : FS_SAMPLER(POINT_MAP_SLOT);
struct PointLight {
    float4 at;       // xyz the light (the landscape's space), w 1 / its range squared
    float4 colour;   // rgb its colour, w 1: specular (the ground adds it after its texture)
    int4 shadow;     // x its map (point_seen's slot), -1 none
};
// The grid as words: per cell two (its lights' first word and their count), then the cells' lights' indices.
struct LightWords {
    uint4 words;
};
StructuredBuffer<PointLight> pointLights : FS_STORAGE(POINT_LIGHTS_SLOT);
StructuredBuffer<LightWords> lightGrid : FS_STORAGE(POINT_GRID_SLOT);
uint grid_word(uint i) {
    uint4 w = lightGrid[i >> 2].words;
    return w[i & 3];
}
// The lights that may reach the landscape point `p`: its cell's first word and count; none outside the grid.
uint2 cell_lights(float3 p) {
    float2 c = floor((p.xy - points.gridOrigin) * points.gridScale);
    if (!(all(c >= 0.0) && all(c < float2(points.gridSize)))) return uint2(0, 0);
    uint cell = uint(c.y) * uint(points.gridSize.x) + uint(c.x);
    return uint2(grid_word(cell * 2), grid_word(cell * 2 + 1));
}
static const int kPointTaps = 8;
static const float kPointRadius = 1.5;         // the filter's radius, texels
static const float kPointNormalOffset = 1.5;   // the receiver's point moved along its normal, texels
static const float kPointDepthBias = 1.0;      // texels
static const float kNoPointFront = 1e29;       // MeshBatch::pointFront's none (kNoFront)

// A light's map row (slot, -1 none) as an index into the four rows' arrays: 0 for none.
int point_index(int slot) { return clamp(slot, 0, 3); }

// How much of the light with map `slot` reaches the landscape point `p`: 1 all .. 0 none. `n`: the unit normal the
// point is moved along before the lookup (a texel's size at its distance times kPointNormalOffset; zero: not moved).
// `front`: a unit's nearest distance to the light (the sun map's unit rule: the unit compares from there, so only what
// lies nearer the light than all of it shadows it; at or inside the near distance: the light is the unit's own,
// nothing hides it), kNoPointFront for the rest. The face is the one the point's direction from the light leaves the
// cube by, as render_point_shadows.cpp's kCubeFaces (x across, y up, the axis forward); `turn` turns the filter's
// disc per pixel. A shadow fading in or out shows its share (PointLightParams::shadows' w).
float point_seen(int slot, float3 p, float3 n, float front, float2 turn) {
    // A light without a map (slot -1) is still worked out by a lane that does not use the answer (ground_shade's ?:
    // evaluates both sides; a lane its caller's branch masks off runs on a software device): kept inside the arrays.
    // WARP read index -1 as the block's 4096th vector, past SDL's 32 KB uniform buffer, and removed its device on the
    // access violation.
    slot = point_index(slot);
    float4 at = points.shadows[slot];
    float texels = points.texels;
    float nearDistance = points.nearDistance;
    float3 v = p - at.xyz;
    float len = length(v);
    if (front <= nearDistance || len <= nearDistance) return 1.0;
    v += n * (kPointNormalOffset * 2.0 * len / texels);
    float3 a = abs(v);
    float face, w;
    float2 xy;
    if (a.x >= a.y && a.x >= a.z) {
        face = v.x > 0.0 ? 0.0 : 1.0;
        w = a.x;
        xy = float2(v.x > 0.0 ? v.y : -v.y, v.z);
    } else if (a.y >= a.z) {
        face = v.y > 0.0 ? 2.0 : 3.0;
        w = a.y;
        xy = float2(v.y > 0.0 ? -v.x : v.x, v.z);
    } else {
        face = v.z > 0.0 ? 4.0 : 5.0;
        w = a.z;
        xy = float2(v.x, v.z > 0.0 ? v.y : -v.y);
    }
    w = max(w, 1e-6);
    float compared = max(w * min(1.0, front / max(length(v), 1e-6)), nearDistance);
    float4 d = points.depth[slot];
    // The depth at `compared` less kPointDepthBias texels' size there (d.y < 0: nearer is less).
    float z = d.x + d.y / compared + d.y * (kPointDepthBias * 2.0 / texels) / compared;
    float margin = (kPointRadius + 1.0) / texels;
    float2 uv = clamp(float2(xy.x, -xy.y) / w * 0.5 + 0.5, margin, 1.0 - margin);
    float2 tile = float2(face, float(slot));
    float2 scale = float2(1.0 / 6.0, 1.0 / 4.0);
    float lit = 0.0;
    for (int k = 0; k < kPointTaps; ++k) {
        float2 o = shadow_disc(k, kPointTaps, turn) * (kPointRadius / texels);
        lit += point_shadow_map.SampleCmpLevelZero(point_shadow_smp, (tile + uv + o) * scale, z);
    }
    return lerp(1.0, lit / kPointTaps, at.w);
}
