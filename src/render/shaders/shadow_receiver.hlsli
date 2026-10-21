// The shadow map's receivers' part (render_shadows.cpp; included in the terrain and mesh shaders): how
// much of the sun the map hides at a pixel, times the strength. `s` is the pixel's position in the map's
// clip space, `dsx` / `dsy` its screen derivatives (taken before any discard), `pixel` its screen
// position. Surfaces turned away from the sun (their face normal, towards the camera) are left as the
// game lit them. Every comparison follows the receiver's plane (its depth slope across the map, clamped).
// Soft edges (R16j2), contact-hardening as PCSS. `range` holds, per kRangeCell-texel cell of the map,
// the nearest and the farthest depth of the cells within the widest filter's reach (x, y) and the
// cell's own nearest (z). A pixel nearer than every depth around is lit, one farther than every depth
// around is shadowed. For the others (near an edge) the nearest depth around bounds the filter's
// radius: the distance along the light to it times the sun's chosen size (soft.x), from soft.z to
// soft.y. When that bound is wider than a cell, a blocker search narrows it: kSearchTaps cells on a
// disc of the bound and the pixel's own give the mean distance from the receiver to the nearest
// caster in each cell that hides it, the radius that distance times soft.x (so a shadow is sharp
// where its caster meets the ground). Then bilinear comparisons on a disc of the radius:
// kFilterTaps, or kFewTaps on a narrow one. Both discs are Vogel spirals turned per pixel
// (interleaved gradient noise of the screen position): the few taps make a fine grain instead of
// bands. The search reads the small cell image, not the map: the map's texels far apart and turned
// per pixel cost far more.
// The cells' divisions are of int on purpose (a search tap's cell can be left of or above the map,
// then clamped): fxc's note on them (X3556, slower than uint) would fail the build (/WX).
#pragma warning(disable : 3556)
static const int kSearchTaps = 8;
static const int kFilterTaps = 16;
static const int kFewTaps = 8;
static const int kShadowCell = 16;   // kRangeCell
float2 shadow_disc(int k, int n, float2 turn) {
    float r = sqrt((k + 0.5) / n);
    float a = k * 2.39996323;   // the golden angle
    return mul(float2x2(turn.x, -turn.y, turn.y, turn.x), float2(cos(a), sin(a)) * r);
}
float shadow_filter(Texture2D<float> map, SamplerComparisonState smp, float2 uv, float z, float2 slope,
                    float radius, float2 turn, int n) {
    float lit = 0.0;
    for (int j = 0; j < n; ++j) {
        float2 o = shadow_disc(j, n, turn) * radius;
        lit += map.SampleCmpLevelZero(smp, uv + o, z + dot(slope, o));
    }
    return 1.0 - lit / n;
}
float shadow_amount(Texture2D<float> map, SamplerComparisonState smp, Texture2D<float4> range, float3 s,
                    float3 dsx, float3 dsy, float4 on, float4 scale, float4 cam, float4 soft, float2 pixel) {
    if (on.x < 0.5) return 0.0;
    float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || s.z > 1.0) return 0.0;
    float3 n = cross(dsx * scale.xyz, dsy * scale.xyz);
    float len = length(n);
    float facing = 1.0;
    if (len > 1e-20) {
        n /= len;
        if (dot(n, (cam.xyz - s) * scale.xyz) < 0.0) n = -n;
        facing = saturate(-n.z * 6.0);   // the sun's light travels along +z
    }
    if (facing <= 0.0) return 0.0;
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -scale.w, scale.w);
    float z = s.z - on.w;
    int cells = int(soft.w) / kShadowCell - 1;
    float4 around = range.Load(int3(clamp(int2(uv * soft.w) / kShadowCell, 0, cells), 0));
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;   // the receiver's plane across the search
    if (around.x >= z + reach) return 0.0;
    if (around.y < z - reach) return facing * on.y;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    float turn_angle = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float2 turn = float2(cos(turn_angle), sin(turn_angle));
    float radius = widest;
    [branch] if (widest * soft.w > kShadowCell) {
        float apart = 0.0, hidden = 0.0;
        if (around.z < z) { apart += z - around.z; hidden += 1.0; }
        [unroll] for (int k = 0; k < kSearchTaps; ++k) {
            float2 o = shadow_disc(k, kSearchTaps, turn) * widest;
            float zo = z + dot(slope, o);
            float d = range.Load(int3(clamp(int2((uv + o) * soft.w) / kShadowCell, 0, cells), 0)).z;
            if (d < zo) { apart += zo - d; hidden += 1.0; }
        }
        if (hidden == 0.0) return 0.0;
        radius = clamp(apart / hidden * soft.x, soft.z, widest);
    }
    float2 turn2 = mul(float2x2(turn.x, -turn.y, turn.y, turn.x), float2(0.70710678, 0.70710678));
    float part;
    [branch] if (radius * soft.w < 4.0) part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFewTaps);
    else part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFilterTaps);
    return part * facing * on.y;
}
