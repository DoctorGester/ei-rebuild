// The shadow map's receivers' part (render_shadows.cpp; included in the terrain and mesh shaders): how
// much of the sun the map hides at a pixel, times the strength. `s` is the pixel's position in the map's
// clip space, `dsx` / `dsy` its screen derivatives (taken before any discard), `pixel` its screen
// position. Every comparison follows the receiver's plane (its depth slope across the map, clamped).
// Facing (R22): a receiver's own object (cam.w, its caster id; 0 for the ground and for objects that cast
// nothing) shadows it only where its face (the face normal from the derivatives, towards the camera) is
// turned to the sun: a surface turned away is always behind its own object's front, and the game lit
// it (the rule keeps it as the game lit it, without acne). Every other object's shadow darkens it
// whichever way it faces: a flora mound's faces in a pine's shadow, lit by the game alike, cut off at
// their edges before (`ids`: the caster id of each map texel, compared per tap where facing < 1).
// cam.w below 0 (the harness's "shadowfacing all"): every caster counts as the receiver's own, as before.
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
float shadow_filter(Texture2D<float> map, SamplerComparisonState smp, float2 uv, float z, float2 slope,
                    float radius, float2 turn, int n) {
    float lit = 0.0;
    for (int j = 0; j < n; ++j) {
        float2 o = shadow_disc(j, n, turn) * radius;
        lit += map.SampleCmpLevelZero(smp, uv + o, z + dot(slope, o));
    }
    return 1.0 - lit / n;
}
// The id path (a receiver turned partly or wholly away from the sun with an id of its own): point taps,
// each hidden by another object counting 1 and by its own object `facing`.
float shadow_filter_ids(Texture2D<float> map, Texture2D<uint> ids, float2 uv, float z, float2 slope, float radius,
                        float2 turn, int n, uint self, bool every_own, float facing, float size) {
    float hidden = 0.0;
    int last = int(size) - 1;
    for (int j = 0; j < n; ++j) {
        float2 o = shadow_disc(j, n, turn) * radius;
        int3 t = int3(clamp(int2((uv + o) * size), 0, last), 0);
        if (map.Load(t) < z + dot(slope, o)) hidden += ids.Load(t) != self && !every_own ? 1.0 : facing;
    }
    return hidden / n;
}
float shadow_amount(Texture2D<float> map, SamplerComparisonState smp, Texture2D<float4> range, SamplerState range_smp,
                    Texture2D<uint> ids, float3 s, float3 dsx, float3 dsy, float4 on, float4 scale, float4 cam,
                    float4 soft, float2 pixel) {
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
    uint self = uint(max(cam.w, 0.0));
    bool every_own = cam.w < 0.0;
    if (self == 0 && !every_own) facing = 1.0;   // nothing of its own in the map: every occluder is another's
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -scale.w, scale.w);
    float z = s.z - on.w;
    float4 around = range.SampleLevel(range_smp, uv, 0.0);
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;   // the receiver's plane across the search
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    float turn_angle = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float2 turn = float2(cos(turn_angle), sin(turn_angle));
    [branch] if (facing < 1.0) {
        // Whose shadow it is matters: per tap, over the widest radius the nearest depth around allows
        // (the blocker search's cells do not tell objects apart).
        float2 turn1 = mul(float2x2(turn.x, -turn.y, turn.y, turn.x), float2(0.70710678, 0.70710678));
        int n = widest * soft.w < 4.0 ? kFewTaps : kFilterTaps;
        return shadow_filter_ids(map, ids, uv, z, slope, widest, turn1, n, self, every_own, facing, soft.w) * on.y;
    }
    if (around.y < z - reach) return on.y;
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
    return part * on.y;
}
