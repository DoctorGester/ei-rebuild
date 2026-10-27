// The shadow map's receivers' part (render_shadows.cpp; included in the terrain and mesh shaders): how
// much of the sun the map hides at a pixel (0 .. 1; the receivers darken by it times the strength, on.y,
// or take the sun's light away by it: R16i). `s` is the pixel's position in the map's clip space, `dsx` /
// `dsy` its screen derivatives (taken before any discard), `pixel` its screen position. Every comparison
// follows the receiver's plane (its depth slope across the map, clamped). A receiver weighs it by its
// facing to the sun, so a face turned away is not shadowed (its own object's shadow there would be acne):
// the ground by its normal (R16i), the meshes by their face normal (`shadow_facing`, the derivatives).
// A unit's meshes instead (R16i2) compare from the unit's nearest depth (`front`): no shadow of its own
// (that flipped face by face on a low-poly creature), every other caster's on every face, its sun side
// shaded by its lighting's N.L alone.
// Soft edges (R16j2), contact-hardening as PCSS. `range` holds, per kRangeCell-texel cell of the map,
// the nearest and the farthest depth of the cells within the widest filter's reach and one cell more
// (x, y), and the cell's own nearest (z). It is read filtered (`range_smp`: bilinear between the four
// cells around the pixel; R25). Each of those four cells' bounds holds for every texel the pixel's taps
// read, so their weighted mean does too, and the bounds change smoothly across the map instead of
// stepping at the cells' edges (read per cell, they made cell-sized squares in a penumbra, which crawled
// while the sun turned the map's texels across the ground: the user's shimmer). A pixel nearer than
// every depth around is lit, one farther than every depth around is shadowed. For the others (near an
// edge) the nearest depth around bounds the filter's radius: the distance along the light to it times
// the sun's chosen size (soft.x), from soft.z to soft.y. When that bound is wider than half a cell, a blocker
// search narrows it: kSearchTaps points on a disc of the bound and the pixel's own give the mean
// distance from the receiver to the nearest caster around each (the cells' own nearest, filtered) that
// hides it, the radius that distance times soft.x (so a shadow is sharp where its caster meets the
// ground). From half a cell to a cell the searched radius is blended in from the bound, so the radius
// does not step where the search starts. Then bilinear comparisons on a disc of the radius:
// kFilterTaps, or kFewTaps on a narrow one. Both discs are Vogel spirals turned per pixel (interleaved
// gradient noise of the screen position): the few taps make a fine grain instead of bands. The search
// reads the small cell image, not the map: the map's texels far apart and turned per pixel cost far more.
static const int kSearchTaps = 8;
static const int kFilterTaps = 16;
static const int kFewTaps = 8;
static const float kShadowCell = 16.0;   // kRangeCell
float2 shadow_disc(int k, int n, float2 turn) {
    float r = sqrt((k + 0.5) / n);
    float a = k * 2.39996323;   // the golden angle
    return mul(float2x2(turn.x, -turn.y, turn.y, turn.x), float2(cos(a), sin(a)) * r);
}
// The discs' turn at a screen pixel (interleaved gradient noise): cosine and sine.
float2 pixel_turn(float2 pixel) {
    float a = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return float2(cos(a), sin(a));
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
// A face's turn to the sun from its face normal (the derivatives, turned towards the camera `cam`): 1
// facing it, 0 turned away.
float shadow_facing(float3 s, float3 dsx, float3 dsy, float4 scale, float4 cam) {
    float3 n = cross(dsx * scale.xyz, dsy * scale.xyz);
    float len = length(n);
    if (len <= 1e-20) return 1.0;
    n /= len;
    if (dot(n, (cam.xyz - s) * scale.xyz) < 0.0) n = -n;
    return saturate(-n.z * 6.0);   // the sun's light travels along +z
}
float shadow_amount(Texture2D<float> map, SamplerComparisonState smp, Texture2D<float4> range, SamplerState range_smp,
                    float3 s, float3 dsx, float3 dsy, float4 on, float4 scale, float4 soft, float2 pixel,
                    float front) {
    if (on.x < 0.5) return 0.0;
    float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || s.z > 1.0) return 0.0;
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -scale.w, scale.w);
    float z = s.z - on.w;
    // A unit's mesh (R16i2): compared as a plane facing the light at its object's nearest depth (`front`;
    // above 1: not a unit), so only what lies nearer the sun than all of the unit shadows it.
    if (front <= 1.0) {
        z = min(s.z, front) - on.w;
        slope = 0.0;
    }
    float4 around = range.SampleLevel(range_smp, uv, 0.0);
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;   // the receiver's plane across the search
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    float2 turn = pixel_turn(pixel);
    if (around.y < z - reach) return 1.0;
    float radius = widest;
    float searched = saturate(widest * soft.w * 2.0 / kShadowCell - 1.0);   // 0 up to half a cell, 1 from a cell
    [branch] if (searched > 0.0) {
        float apart = 0.0, hidden = 0.0;
        if (around.z < z) { apart += z - around.z; hidden += 1.0; }
        [unroll] for (int k = 0; k < kSearchTaps; ++k) {
            float2 o = shadow_disc(k, kSearchTaps, turn) * widest;
            float zo = z + dot(slope, o);
            float d = range.SampleLevel(range_smp, uv + o, 0.0).z;
            if (d < zo) { apart += zo - d; hidden += 1.0; }
        }
        if (hidden == 0.0 && searched >= 1.0) return 0.0;
        float found = hidden > 0.0 ? clamp(apart / hidden * soft.x, soft.z, widest) : soft.z;
        radius = lerp(widest, found, searched);
    }
    float2 turn2 = mul(float2x2(turn.x, -turn.y, turn.y, turn.x), float2(0.70710678, 0.70710678));
    float part;
    [branch] if (radius * soft.w < 4.0) part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFewTaps);
    else part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFilterTaps);
    return part;
}
