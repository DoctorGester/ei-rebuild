// The shadow map's receivers' part (included in the terrain and mesh programs): how much of the sun the map hides at
// a pixel (0 .. 1). `s`: the pixel in the map's clip space, `dsx` / `dsy` its screen derivatives (taken before any
// discard). Soft edges as PCSS. See docs/notes/renderer.md, "The renderer's source files". The including program
// names its slots: SHADOW_BLOCK_SLOT, SHADOW_MAP_SLOT and SHADOW_RANGE_SLOT.
#include "bindings.hlsli"
struct ShadowParams {
    int on;               // the map is read (the clouds' and the roofs' parts have their own)
    float strength;       // the share of the light a full shadow takes
    int shadowView;       // the shadow alone (SetShadowView)
    float depthBias;      // constant
    float3 scale;         // shadow clip units to the map's world units (normals)
    float largestSlope;   // of the depth
    float3 camera;        // the camera in shadow clip space
    float softness;       // the filter's radius (across the light, in uv) per unit of blocker distance (clip z)
    float searchRadius;   // the blocker search's (the widest filter)
    float narrowest;      // the narrowest filter
    float texels;         // the map's, a side
    float3 sun;           // the light's direction in the landscape's space (travelling, down)
    float stretch;        // the most a disc across the light is stretched on the map
    float4 disc;          // a disc across the light onto the map: uv = (x * a + y * b, x * c + y * d) (xyzw: a b c d)
    float4 discInverse;   // the same back from uv
    // The cloud shadows (cloud_shadow.hlsli): the map's clip space to each layer's uv (times the scale, plus the
    // shift: the layer's uv at the map's clip origin), the cover's threshold and how much of the sun a full cloud
    // hides (0: none).
    float cloudScaleA;
    float cloudScaleB;
    float cloudThreshold;
    float cloudLight;
    float2 cloudShiftA;
    float2 cloudShiftB;
    // The roofs over the weather (roof.hlsli): the map's clip space to the roof map's u, v, depth; a landscape offset
    // (per unit) in them; the filter's step in uv; a receiver this much depth under the highest roof is in the open;
    // the lookup's offset along the receiver's normal, landscape units.
    float4 roofRows[3];
    float3 roofScale;
    float roofStep;
    int roofOn;
    float roofOpenDepth;
    float roofNormalOffset;
    // The map's shadows fade out from this distance to the camera (world units) over the length (none when 0).
    float fadeFrom;
    float fadeLength;
};
ConstantBuffer<ShadowParams> shadow : FS_BLOCK(SHADOW_BLOCK_SLOT);
Texture2D<float> shadow_map : FS_TEXTURE(SHADOW_MAP_SLOT);
SamplerComparisonState shadow_smp : FS_SAMPLER(SHADOW_MAP_SLOT);
Texture2D<float4> shadow_range : FS_TEXTURE(SHADOW_RANGE_SLOT);
SamplerState range_smp : FS_SAMPLER(SHADOW_RANGE_SLOT);   // unused: the range is read with Load

static const int kSearchTaps = 8;
static const int kFilterTaps = 16;
static const int kFewTaps = 8;
static const float kShadowCell = 16.0;   // kRangeCell
static const float kQuadReach = 0.25;    // the most a quad neighbour's result moves a pixel's (shadow_amount)

// The range at `uv`, bilinear between the four cells around it and clamped at the map's edges, done by hand: a
// filtered read of RGBA32F is optional on some devices.
float4 range_at(float2 uv) {
    uint w, h;
    shadow_range.GetDimensions(w, h);
    int2 size = int2(w, h);
    float2 p = uv * float2(size) - 0.5;
    float2 base = floor(p);
    float2 f = p - base;
    int2 lo = clamp(int2(base), 0, size - 1), hi = clamp(int2(base) + 1, 0, size - 1);
    float4 a = shadow_range.Load(int3(lo, 0));
    float4 b = shadow_range.Load(int3(hi.x, lo.y, 0));
    float4 c = shadow_range.Load(int3(lo.x, hi.y, 0));
    float4 d = shadow_range.Load(int3(hi, 0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

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
// The filter's taps: tap k of a pixel of its 2 x 2 quad (`quad` 0 .. 3) lies on a Vogel spiral of n taps, its radius
// moved out by a quarter step per quad pixel and the spiral turned by a quarter turn, so the quad's four together cover
// the disc 4n times as densely; `turn` is the quad's.
float2 shadow_quad_disc(int k, int n, float quad, float2 turn) {
    float r = sqrt((k + (quad + 0.5) * 0.25) / n);
    float a = k * 2.39996323 + quad * 1.57079633;
    return turned(turn, float2(cos(a), sin(a)) * r);
}
// A disc's point across the light (as the sun's own disc stands) on the map, in uv.
float2 on_map(float2 across) {
    return float2(dot(shadow.disc.xy, across), dot(shadow.disc.zw, across));
}
// The filter over a disc of `radius` (across the light) weighted 1 - r^2 (r: the distance from the centre over the
// radius). Returns the share hidden and in `grad` its slope per unit across the light, from the same taps: the
// weight's own slope times each tap's difference from the mean (the neighbours in the quad move the mean to their own
// centres by it, shadow_amount).
float shadow_filter(float2 uv, float z, float2 slope, float radius, float2 turn, float quad, int n, out float2 grad) {
    float lit = 0.0, weights = 0.0;
    float2 moment = 0.0, centre = 0.0;
    for (int k = 0; k < n; ++k) {
        float2 unit = shadow_quad_disc(k, n, quad, turn);
        float2 o = on_map(unit * radius);
        float hit = shadow_map.SampleCmpLevelZero(shadow_smp, uv + o, z + dot(slope, o));
        float w = 1.0 - dot(unit, unit);
        lit += hit * w;
        weights += w;
        moment += unit * hit;
        centre += unit;
    }
    lit /= weights;
    // d/dc of sum(w hit) / sum(w) with w = 1 - |o - c|^2 / R^2: 2 / R times sum(unit (hit - lit)) / sum(w).
    grad = (moment - centre * lit) * (-2.0 / (radius * weights));
    return 1.0 - lit;
}
// The map's world units (the landscape's x and y along the light onto a level plane, the distance along the light)
// to the landscape's space (offsets).
float3 map_to_landscape(float3 m) {
    float3 d = shadow.sun;
    float along = m.z - d.x * m.x - d.y * m.y;
    return float3(m.x + d.x * along, m.y + d.y * along, d.z * along);
}
// A face's turn to the sun from its face normal (the derivatives, turned towards the camera): 1 facing it, 0 turned
// away.
float shadow_facing(float3 s, float3 dsx, float3 dsy) {
    float3 n = cross(map_to_landscape(dsx * shadow.scale), map_to_landscape(dsy * shadow.scale));
    float len = length(n);
    if (len <= 1e-20) return 1.0;
    n /= len;
    if (dot(n, map_to_landscape((shadow.camera - s) * shadow.scale)) < 0.0) n = -n;
    return saturate(dot(n, -shadow.sun) * 6.0);
}
// One pixel's share hidden, with `grad` its slope and `radius` the filter's (0 when decided without it), across the
// light. `least`: the narrowest filter.
float shadow_pixel(float2 uv, float z, float2 slope, float least, float2 turn, float quad, out float2 grad,
                   out float radius) {
    grad = 0.0;
    radius = 0.0;
    float4 around = range_at(uv);
    // The receiver's plane across the search.
    float reach = (abs(slope.x) + abs(slope.y)) * shadow.searchRadius * shadow.stretch;
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * shadow.softness, least, shadow.searchRadius);
    if (around.y < z - reach) return 1.0;
    float wide = widest;
    float searched = saturate(widest * shadow.texels * 2.0 / kShadowCell - 1.0);   // 0 up to half a cell, 1 from a cell
    if (searched > 0.0) {
        float apart = 0.0, hidden = 0.0;
        if (around.z < z) {
            apart += z - around.z;
            hidden += 1.0;
        }
        for (int k = 0; k < kSearchTaps; ++k) {
            float2 o = on_map(shadow_quad_disc(k, kSearchTaps, quad, turn) * widest);
            float zo = z + dot(slope, o);
            float d = range_at(uv + o).z;
            if (d < zo) {
                apart += zo - d;
                hidden += 1.0;
            }
        }
        if (hidden == 0.0 && searched >= 1.0) return 0.0;
        float found = hidden > 0.0 ? clamp(apart / hidden * shadow.softness, least, widest) : least;
        wide = lerp(widest, found, searched);
    }
    radius = wide;
    float2 turn2 = turned(turn, float2(0.70710678, 0.70710678));
    int taps = radius * shadow.texels < 4.0 ? kFewTaps : kFilterTaps;
    return shadow_filter(uv, z, slope, radius, turn2, quad, taps, grad);
}
// The share of the sun hidden at a pixel. A filter wider than a few pixels takes the taps of the pixel's whole 2 x 2
// quad (each neighbour's result moved to this pixel's centre by its own gradient), four times as many for the cost of
// the gradient: the grain of a few taps on a wide disc, which changes as the shadow moves over it, is that much finer.
// A filter is never narrower than half a pixel, so a hard edge is smoothed at the screen's resolution and does not
// crawl from pixel to pixel.
float shadow_amount(float3 s, float3 dsx, float3 dsy, float2 pixel, float front) {
    if (shadow.on == 0) return 0.0;
    float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    float2 ux = float2(dsx.x * 0.5, -dsx.y * 0.5), uy = float2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    float2 slope = 0.0;
    if (abs(det) > 1e-24) slope = float2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -shadow.largestSlope, shadow.largestSlope);
    float z = s.z - shadow.depthBias;
    // A unit's mesh: compared as a plane facing the light at its object's nearest depth (`front`; above 1: not a
    // unit), so only what lies nearer the sun than all of the unit shadows it.
    if (front <= 1.0) {
        z = min(s.z, front) - shadow.depthBias;
        slope = 0.0;
    }
    // A pixel's size across the light (the disc's units).
    float2x2 back = float2x2(shadow.discInverse.xy, shadow.discInverse.zw);
    float foot = sqrt(abs(det * determinant(back)));
    uint2 at = uint2(pixel);
    float quad = float((at.x & 1u) + 2u * (at.y & 1u));
    float2 turn = pixel_turn(floor(pixel * 0.5));
    float h = 0.0, radius = 0.0;
    float2 grad = 0.0;
    if (all(uv >= 0.0) && all(uv <= 1.0) && s.z <= 1.0)
        h = shadow_pixel(uv, z, slope, min(max(shadow.narrowest, foot * 0.5), shadow.searchRadius), turn, quad, grad, radius);
    // The quad's neighbours, each extrapolated to this pixel (fine derivatives: the pixel and its neighbour in the
    // quad's row or column).
    float sx = (at.x & 1u) != 0u ? -1.0 : 1.0, sy = (at.y & 1u) != 0u ? -1.0 : 1.0;
    float2 toX = mul(back, ux * sx), toY = mul(back, uy * sy);   // to the neighbour in x, in y (across the light)
    float ex = h + dot(grad, toX), ey = h + dot(grad, toY), exy = h + dot(grad, toX + toY);
    float fromX = ex + sx * ddx_fine(ex);
    float fromY = ey + sy * ddy_fine(ey);
    float diagonal = exy + sx * ddx_fine(exy);
    float fromXY = diagonal + sy * ddy_fine(diagonal);
    // A neighbour counts at most kQuadReach from the pixel's own: a helper lane past a silhouette or a fold extrapolates
    // its triangle's plane into the air or into the caster.
    float pull = clamp(fromX - h, -kQuadReach, kQuadReach) + clamp(fromY - h, -kQuadReach, kQuadReach) +
                 clamp(fromXY - h, -kQuadReach, kQuadReach);
    float quadMean = saturate(h + pull * 0.25);
    float hidden = lerp(h, quadMean, saturate(radius / max(foot, 1e-20) * (2.0 / 3.0) - 1.0));   // 0 up to 1.5 pixels, 1 from 3
    if (shadow.fadeLength > 0.0) {
        float away = length(map_to_landscape((s - shadow.camera) * shadow.scale));
        hidden *= saturate((shadow.fadeFrom + shadow.fadeLength - away) / shadow.fadeLength);
    }
    return hidden;
}
