// The shadow map's receivers' part (included in the terrain and mesh programs): how much of the sun the map hides at
// a pixel (0 .. 1). `s`: the pixel in the map's clip space, `dsx` / `dsy` its screen derivatives (taken before any
// discard). Soft edges as PCSS. See docs/notes/renderer.md, "The renderer's source files". The including program
// names its slots: SHADOW_BLOCK_SLOT, SHADOW_MAP_SLOT and SHADOW_RANGE_SLOT.
#include "bindings.hlsli"
struct ShadowParams {
    float4 on;       // x: on, y: strength, z: 1 the shadow alone (SetShadowView), w: constant depth bias
    float4 scale;    // xyz: shadow clip units to world units (normals), w: largest depth slope
    float4 camera;   // xyz: the camera in shadow clip space
    float4 soft;     // x: filter radius (uv) per unit of blocker distance (clip z), y: the blocker search's radius
                     // (uv, the widest filter), z: the narrowest filter (uv), w: texels a side
};
ConstantBuffer<ShadowParams> shadow : FS_BLOCK(SHADOW_BLOCK_SLOT);
Texture2D<float> shadow_map : FS_TEXTURE(SHADOW_MAP_SLOT);
SamplerComparisonState shadow_smp : FS_SAMPLER(SHADOW_MAP_SLOT);
Texture2D<float4> shadow_range : FS_TEXTURE(SHADOW_RANGE_SLOT);
SamplerState shadow_linear : FS_SAMPLER(SHADOW_RANGE_SLOT);   // the range, filtered

static const int kSearchTaps = 8;
static const int kFilterTaps = 16;
static const int kFewTaps = 8;
static const float kShadowCell = 16.0;   // kRangeCell

// `v` turned by `turn` (cosine, sine).
float2 turned(float2 turn, float2 v) { return float2(turn.x * v.x - turn.y * v.y, turn.y * v.x + turn.x * v.y); }
float2 shadow_disc(int k, int n, float2 turn) {
    float r = sqrt((k + 0.5) / n);
    float a = k * 2.39996323;   // the golden angle
    return turned(turn, float2(cos(a), sin(a)) * r);
}
// The discs' turn at a screen pixel (interleaved gradient noise): cosine and sine.
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
// A face's turn to the sun from its face normal (the derivatives, turned towards the camera): 1 facing it, 0 turned
// away.
float shadow_facing(float3 s, float3 dsx, float3 dsy) {
    float3 n = cross(dsx * shadow.scale.xyz, dsy * shadow.scale.xyz);
    float len = length(n);
    if (len <= 1e-20) return 1.0;
    n /= len;
    if (dot(n, (shadow.camera.xyz - s) * shadow.scale.xyz) < 0.0) n = -n;
    return saturate(-n.z * 6.0);   // the sun's light travels along +z
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
    // A unit's mesh: compared as a plane facing the light at its object's nearest depth (`front`; above 1: not a
    // unit), so only what lies nearer the sun than all of the unit shadows it.
    if (front <= 1.0) {
        z = min(s.z, front) - shadow.on.w;
        slope = 0.0;
    }
    float4 soft = shadow.soft;
    float4 around = shadow_range.SampleLevel(shadow_linear, uv, 0.0);
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;   // the receiver's plane across the search
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    float2 turn = pixel_turn(pixel);
    if (around.y < z - reach) return 1.0;
    float radius = widest;
    float searched = saturate(widest * soft.w * 2.0 / kShadowCell - 1.0);   // 0 up to half a cell, 1 from a cell
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
