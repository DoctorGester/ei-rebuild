// The ground's and the water's shared functions (terrain_common.hlsli says what the program declares first): in the
// vertex stage the animated tiles' coordinates, the edge fade and the clip, fog and shadow-map coordinates; in the
// fragment stage the receivers (shadow map, point shadows, clouds, light, roof), the page textures, the rain's rings and
// the sky's reflection.
#ifdef EI_VERTEX
static const float kEdgeWidening = 0.2;   // the edge fade's band at least this times the depth
// The map's edge fade (Terrain::EdgeFade: 1 inside, falling to 0 over the band at the edges, by the distance into the
// band from its inner border) with the band widened to kEdgeWidening times the depth `depth`.
float edge_kept(float2 p, float depth) {
    if (vs.edgeBand <= 0.0) return 1.0;
    float band = max(vs.edgeBand, depth * kEdgeWidening);
    float4 inside = float4(p, float2(vs.mapWidth, vs.mapHeight) - p) - band - 1.0;
    return 1.0 - min(length(min(inside, 0.0)), band) / band;
}
// The vertex's texture coordinates: its own, or an animated tile's at its frame now (Terrain::tileSteps steps since the
// tile was built, from its phase). `precise`: the products stay products, so the sums round as the CPU's did when it
// wrote them (SectorData::AnimateTiles; a column's and row's offset of the page, then the tile's own coordinate).
float2 tile_uv(VsIn i) {
    if (i.anim.y < 1.0) return i.uv.xy;
    const int frames = int(i.anim.y);
    const int index = int(i.anim.x) + (int(i.anim.z) + vs.tileSteps % frames) % frames;
    const int row = index / vs.tilesPerRow;
    const int column = index - row * vs.tilesPerRow;
    precise float u0 = float(column) * vs.tileSide;
    precise float v0 = float(row) * vs.tileSide;
    precise float u = u0 + i.uv.x;
    precise float v = v0 + i.uv.y;
    return float2(u, 1.0 - v);
}
float4 clip_position(float3 pos) {
    float4 p = float4(pos, 1.0);
    float4 c = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    c.xy += vs.shift * c.w;   // the half-pixel shift (render_internal.h)
    return c;
}
// The fog factor: the map's edge fade (the vertex's own, Terrain::EdgeFade, and the band widened with the depth) or the
// distance fog, the smallest. `fade`: the vertex's own edge fade, `w` its clip w.
float fog_factor(float fade, float2 xy, float w) {
    return min(min(fade, edge_kept(xy, w * vs.fogDepthPerW)), fog_kept(w, vs.fogStart, vs.fogDensity, vs.fogDepthPerW));
}
float4 shadow_coord(float3 pos, float3 normal) {
    float4 q = float4(pos + normal * vs.normalOffset, 1.0);
    return float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
}
#endif

#ifdef EI_FRAGMENT
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
#include "roof.hlsli"
Texture2DArray<float4> tex : FS_TEXTURE(0);   // the page textures, a layer each
SamplerState smp : FS_SAMPLER(0);
// The sky dome drawn mirrored in the water's plane (render_water.cpp; bilinear, clamped).
Texture2D<float4> sky_image : FS_TEXTURE(SKY_IMAGE_SLOT);
SamplerState sky_smp : FS_SAMPLER(SKY_IMAGE_SLOT);

// The rain's rings and splashes on the puddles and the water (render_weather.cpp; docs/notes/renderer.md section 13,
// item 11b).
static const float kRingCell = 0.8;       // a ring's cell, landscape units
static const float kRingPeriodA = 16.0;   // client ticks a cell's ring lasts, layer A (render_weather.cpp kRingWrap)
static const float kRingPeriodB = 21.0;   // ... layer B
static const float kRingReach = 0.25;     // a ring's radius at its end, in cells (its centre 0.25 or more from the edge)
static const float kRingWidth = 0.07;     // in cells

float3 hash3(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// The rain's rings at `xy` (landscape units), `rain` the share of the cells a drop falls in: the slope they add (xy),
// the drops' splashes (z, 0 .. 1: a small soft spot where a drop has just fallen) and the rings' crests (w, 0 .. 1).
// Two layers of cells, each cell's drop at its own place and time on the client's clock. `footprint`: the landscape
// units a pixel spans; rings narrower than a pixel only shimmer, so they fade out. The crests are shaded apart from the
// slope (rain_dimples): seen from above, the water reflects little of the sky whatever the slope, so by the slope alone
// the rings do not show.
float4 rain_rings(float2 xy, float rain, float footprint) {
    float4 sum = 0.0;
    float seen = 1.0 - smoothstep(0.05, 0.15, footprint / kRingCell);
    if (rain <= 0.0 || seen <= 0.0) return sum;
    [unroll] for (int layer = 0; layer < 2; ++layer) {
        float2 p = xy / kRingCell + (layer == 0 ? 0.0 : float2(0.5, 0.37));
        float2 cell = floor(p);
        float3 h = hash3(float3(cell, layer * 17.0));
        float3 g = hash3(float3(cell + 31.7, layer * 17.0 + 5.0));
        if (g.x >= rain) continue;
        float period = layer == 0 ? kRingPeriodA : kRingPeriodB;
        float age = frac(fs.rainClock / period + h.z);
        float2 d = p - (cell + 0.25 + 0.5 * h.xy);
        float r = length(d);
        float x = (r - age * kRingReach) / kRingWidth;
        float fade = (1.0 - age) * (1.0 - age);
        sum.xy += d / max(r, 1e-4) * (-2.0 * x * exp(-x * x)) * fade * 0.35;
        sum.z = max(sum.z, (1.0 - smoothstep(0.0, 0.12, age)) * (1.0 - smoothstep(0.0, 0.07, r)));
        sum.w = max(sum.w, exp(-x * x) * fade * smoothstep(0.0, 0.1, age));
    }
    return sum * seen;
}

// Water of colour `c` reflecting `sky` with the rain's rings and splashes (rain_rings) on it: a ring's crest and a
// splash catch more of the sky, a little lighter than the water around and never brighter than the sky it reflects.
float3 rain_dimples(float3 c, float3 sky, float4 rings) {
    float3 lighter = max(c, lerp(c, sky, 0.6));
    return lerp(c, lighter, saturate(rings.w * 0.55 + rings.z * 0.5));
}

// About how far a point at depth `d` lies from where the depth reaches 0, in landscape units: positive under the water,
// negative above it. `slope`: the ground's rise per landscape unit there, interpolated from the vertices' normals so
// the distance runs on smoothly across the triangles. Taken per triangle (from the depth's pixel derivatives) it
// jumped at every triangle's edge where the ground bends, and the swamps' edges and muddy rims stepped along the
// triangles in straight lines and notches (scenario render_swamp_shore).
float shore_distance(float d, float slope) {
    return d / max(slope, 0.02);
}

// `r` turned up to at least a little above the horizon (a ripple can tip it below).
float3 upwards(float3 r) { return normalize(float3(r.xy, max(r.z, 0.02))); }

float4 project(row_major float4x4 m, float3 p) {
    float4 q = float4(p, 1.0);
    return float4(dot(m[0], q), dot(m[1], q), dot(m[2], q), dot(m[3], q));
}

// The sky the water (or a puddle) at `p` reflects along `r` (unit, upwards): where the ray meets the dome (taken as an
// ellipsoid of its radius across and its height), looked up in the dome drawn mirrored in the water's plane. Right at
// any water level: the mirrored image is read where it shows that point of the dome.
float3 reflected_sky(float3 p, float3 r) {
    float stretch = fs.domeRadius / max(fs.domeHeight, 1e-3);
    float3 o = p - fs.dome;
    o.z *= stretch;
    float3 d = float3(r.xy, r.z * stretch);
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - fs.domeRadius * fs.domeRadius;
    float t = (-b + sqrt(max(b * b - a * c, 0.0))) / max(a, 1e-6);   // the far meeting: the point lies inside
    float4 q = project(fs.skyView, p + r * max(t, 0.0));
    float2 uv = q.xy / max(q.w, 1e-4) * float2(0.5, -0.5) + 0.5;
    return sky_image.SampleLevel(sky_smp, uv, 0.0).rgb;
}

// The surface's vertex-interpolated normal, unit (up where it has none).
float3 surface_normal(float3 normal) {
    float len = length(normal);
    return len > 1e-6 ? normal / len : float3(0.0, 0.0, 1.0);
}
#endif
