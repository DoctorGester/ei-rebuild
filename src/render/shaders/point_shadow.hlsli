// Point-light shadows (R16i1; render_point_shadows.cpp, PointShadowParams): the cubes of depth faces of
// the few point lights given a map this frame, a light a row of the atlas, a face a column. Included after
// shadow_receiver.hlsli (shadow_disc); the includer defines POINT_SHADOW_B, POINT_SHADOW_T and
// POINT_SHADOW_S, the registers of the parameters, the atlas and its comparison sampler.
cbuffer point_shadow_params : register(POINT_SHADOW_B) {
    float4 point_shadow_on;          // x: the lights with a map, y: texels a face side
    float4 point_shadow_eye;         // xyz: the camera (the landscape's space)
    float4 point_shadow_at[4];       // xyz: the light (the landscape's space), w: the faces' near distance
    float4 point_shadow_depth[4];    // a face's depth for a distance d along its axis: x + y / d
};
Texture2D<float> point_shadow_map : register(POINT_SHADOW_T);
SamplerComparisonState point_shadow_smp : register(POINT_SHADOW_S);
static const int kPointTaps = 8;
static const float kPointRadius = 1.5;          // the filter's radius, texels
static const float kPointNormalOffset = 1.5;    // the receiver's point moved along its normal, texels
static const float kPointDepthBias = 1.0;       // texels
static const float kNoPointFront = 1e29;        // MeshBatch::pointFront's none (kNoFront)

// How much of the light with map `slot` reaches the landscape point `p`: 1 all .. 0 none. `n`: the unit
// normal the point is moved along before the lookup (a texel's size at its distance times
// kPointNormalOffset; zero: not moved). `front`: a unit's nearest distance to the light (R16i2's rule:
// the unit compares from there, so only what lies nearer the light than all of it shadows it; at or
// inside the near distance: the light is the unit's own, nothing hides it), kNoPointFront for the rest.
// The face is the one the point's direction from the light leaves the cube by, as render_point_shadows.cpp's
// kCubeFaces (x across, y up, the axis forward); `turn` turns the filter's disc per pixel.
float point_seen(int slot, float3 p, float3 n, float front, float2 turn) {
    float4 at = point_shadow_at[slot];
    float texels = point_shadow_on.y;
    float3 v = p - at.xyz;
    float len = length(v);
    if (front <= at.w || len <= at.w) return 1.0;
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
    float compared = max(w * min(1.0, front / max(length(v), 1e-6)), at.w);
    float4 d = point_shadow_depth[slot];
    // The depth at `compared` less kPointDepthBias texels' size there (d.y < 0: nearer is less).
    float z = d.x + d.y / compared + d.y * (kPointDepthBias * 2.0 / texels) / compared;
    float margin = (kPointRadius + 1.0) / texels;
    float2 uv = clamp(float2(xy.x, -xy.y) / w * 0.5 + 0.5, margin, 1.0 - margin);
    float2 tile = float2(face, slot);
    float2 scale = float2(1.0 / 6.0, 1.0 / 4.0);
    float lit = 0.0;
    [unroll] for (int k = 0; k < kPointTaps; ++k) {
        float2 o = shadow_disc(k, kPointTaps, turn) * (kPointRadius / texels);
        lit += point_shadow_map.SampleCmpLevelZero(point_shadow_smp, (tile + uv + o) * scale, z);
    }
    return lit / kPointTaps;
}
