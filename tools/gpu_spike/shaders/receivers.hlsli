// The receivers' shared part of the terrain and mesh shaders for SDL_GPU (the spike): the sun's shadow map
// (shadow_receiver.glsl) and the point-light shadows (point_shadow.glsl), as src/render/shaders has them in
// GLSL. HLSL restored from a861650b3 (the last commit before MAC3e's sokol-shdc move) and brought up to the
// GLSL's blocks. SDL_GPU's fragment bindings: textures and samplers (t, s) in space2, uniform blocks (b) in
// space3; the including shader names the slots (SHADOW_*_SLOT, POINT_*_SLOT).
#define EI_CAT(a, b) a##b
#define FS_TEXTURE(n) register(EI_CAT(t, n), space2)
#define FS_SAMPLER(n) register(EI_CAT(s, n), space2)
#define FS_BLOCK(n) register(EI_CAT(b, n), space3)
#define VS_BLOCK(n) register(EI_CAT(b, n), space1)

struct ShadowParams {
    float4 on;       // x: on, y: strength, z: 1 the shadow alone (SetShadowView), w: constant depth bias
    float4 scale;    // xyz: shadow clip units to world units (normals), w: largest depth slope
    float4 camera;   // xyz: the camera in shadow clip space
    float4 soft;     // x: filter radius per blocker distance, y: the search's radius, z: narrowest, w: texels a side
};
ConstantBuffer<ShadowParams> shadow : FS_BLOCK(SHADOW_BLOCK_SLOT);
Texture2D<float> shadow_map : FS_TEXTURE(SHADOW_MAP_SLOT);
SamplerComparisonState shadow_smp : FS_SAMPLER(SHADOW_MAP_SLOT);
Texture2D<float4> shadow_range : FS_TEXTURE(SHADOW_RANGE_SLOT);
SamplerState shadow_linear : FS_SAMPLER(SHADOW_RANGE_SLOT);

static const int kSearchTaps = 8;
static const int kFilterTaps = 16;
static const int kFewTaps = 8;
static const float kShadowCell = 16.0;

// GLSL's mat2(turn.x, turn.y, -turn.y, turn.x) * v.
float2 turned(float2 turn, float2 v) { return float2(turn.x * v.x - turn.y * v.y, turn.y * v.x + turn.x * v.y); }
float2 shadow_disc(int k, int n, float2 turn) {
    float r = sqrt((k + 0.5) / n);
    float a = k * 2.39996323;
    return turned(turn, float2(cos(a), sin(a)) * r);
}
float2 pixel_turn(float2 pixel) {
    float a = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return float2(cos(a), sin(a));
}
float shadow_filter(float2 uv, float z, float2 slope, float radius, float2 turn, int n) {
    float lit = 0.0;
    for (int j = 0; j < n; ++j) {
        float2 o = shadow_disc(j, n, turn) * radius;
        lit += shadow_map.SampleCmpLevelZero(shadow_smp, uv + o, z + dot(slope, o));
    }
    return 1.0 - lit / n;
}
float shadow_facing(float3 s, float3 dsx, float3 dsy) {
    float3 n = cross(dsx * shadow.scale.xyz, dsy * shadow.scale.xyz);
    float len = length(n);
    if (len <= 1e-20) return 1.0;
    n /= len;
    if (dot(n, (shadow.camera.xyz - s) * shadow.scale.xyz) < 0.0) n = -n;
    return saturate(-n.z * 6.0);
}
float shadow_amount(float3 s, float3 dsx, float3 dsy, float2 pixel, float front) {
    if (shadow.on.x < 0.5) return 0.0;
    float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || s.z > 1.0) return 0.0;
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -shadow.scale.w, shadow.scale.w);
    float z = s.z - shadow.on.w;
    if (front <= 1.0) {
        z = min(s.z, front) - shadow.on.w;
        slope = 0.0;
    }
    float4 soft = shadow.soft;
    float4 around = shadow_range.SampleLevel(shadow_linear, uv, 0.0);
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    float2 turn = pixel_turn(pixel);
    if (around.y < z - reach) return 1.0;
    float radius = widest;
    float searched = saturate(widest * soft.w * 2.0 / kShadowCell - 1.0);
    if (searched > 0.0) {
        float apart = 0.0, hidden = 0.0;
        if (around.z < z) {
            apart += z - around.z;
            hidden += 1.0;
        }
        for (int k = 0; k < kSearchTaps; ++k) {
            float2 o = shadow_disc(k, kSearchTaps, turn) * widest;
            float zo = z + dot(slope, o);
            float d = shadow_range.SampleLevel(shadow_linear, uv + o, 0.0).z;
            if (d < zo) {
                apart += zo - d;
                hidden += 1.0;
            }
        }
        if (hidden == 0.0 && searched >= 1.0) return 0.0;
        float found = hidden > 0.0 ? clamp(apart / hidden * soft.x, soft.z, widest) : soft.z;
        radius = lerp(widest, found, searched);
    }
    float2 turn2 = turned(turn, float2(0.70710678, 0.70710678));
    if (radius * soft.w < 4.0) return shadow_filter(uv, z, slope, radius, turn2, kFewTaps);
    return shadow_filter(uv, z, slope, radius, turn2, kFilterTaps);
}

struct PointShadowParams {
    float4 on;          // x: the lights with a map, y: texels a face side
    float4 eye;         // xyz: the camera (the landscape's space)
    float4 lights[4];   // xyz: the light, w: the faces' near distance
    float4 depth[4];    // a face's depth for a distance d along its axis: x + y / d
};
ConstantBuffer<PointShadowParams> pointShadow : FS_BLOCK(POINT_BLOCK_SLOT);
Texture2D<float> point_shadow_map : FS_TEXTURE(POINT_MAP_SLOT);
SamplerComparisonState point_shadow_smp : FS_SAMPLER(POINT_MAP_SLOT);
static const int kPointTaps = 8;
static const float kPointRadius = 1.5;
static const float kPointNormalOffset = 1.5;
static const float kPointDepthBias = 1.0;
static const float kNoPointFront = 1e29;

float point_seen(int slot, float3 p, float3 n, float front, float2 turn) {
    float4 at = pointShadow.lights[slot];
    float texels = pointShadow.on.y;
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
    float4 d = pointShadow.depth[slot];
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
    return lit / kPointTaps;
}
