// Point-light shadows (render_point_shadows.cpp, PreparePointShadows): the cubes of depth faces of the few point
// lights given a map this frame, a light a row of the atlas, a face a column. Included after shadow_receiver
// (shadow_disc) by the terrain and mesh shaders.
@block point_shadow
layout(binding = 4) uniform point_shadow_params {
    vec4 on;          // x: the lights with a map, y: texels a face side
    vec4 eye;         // xyz: the camera (the landscape's space)
    vec4 lights[4];   // xyz: the light (the landscape's space), w: the faces' near distance
    vec4 depth[4];    // a face's depth for a distance d along its axis: x + y / d
} pointShadow;
layout(binding = 5) uniform texture2D point_shadow_map;
layout(binding = 5) uniform samplerShadow point_shadow_smp;
const int kPointTaps = 8;
const float kPointRadius = 1.5;         // the filter's radius, texels
const float kPointNormalOffset = 1.5;   // the receiver's point moved along its normal, texels
const float kPointDepthBias = 1.0;      // texels
const float kNoPointFront = 1e29;       // MeshBatch::pointFront's none (kNoFront)

// How much of the light with map `slot` reaches the landscape point `p`: 1 all .. 0 none. `n`: the unit
// normal the point is moved along before the lookup (a texel's size at its distance times
// kPointNormalOffset; zero: not moved). `front`: a unit's nearest distance to the light (the sun map's unit rule:
// the unit compares from there, so only what lies nearer the light than all of it shadows it; at or
// inside the near distance: the light is the unit's own, nothing hides it), kNoPointFront for the rest.
// The face is the one the point's direction from the light leaves the cube by, as render_point_shadows.cpp's
// kCubeFaces (x across, y up, the axis forward); `turn` turns the filter's disc per pixel.
float point_seen(int slot, vec3 p, vec3 n, float front, vec2 turn) {
    vec4 at = pointShadow.lights[slot];
    float texels = pointShadow.on.y;
    vec3 v = p - at.xyz;
    float len = length(v);
    if (front <= at.w || len <= at.w) return 1.0;
    v += n * (kPointNormalOffset * 2.0 * len / texels);
    vec3 a = abs(v);
    float face, w;
    vec2 xy;
    if (a.x >= a.y && a.x >= a.z) {
        face = v.x > 0.0 ? 0.0 : 1.0;
        w = a.x;
        xy = vec2(v.x > 0.0 ? v.y : -v.y, v.z);
    } else if (a.y >= a.z) {
        face = v.y > 0.0 ? 2.0 : 3.0;
        w = a.y;
        xy = vec2(v.y > 0.0 ? -v.x : v.x, v.z);
    } else {
        face = v.z > 0.0 ? 4.0 : 5.0;
        w = a.z;
        xy = vec2(v.x, v.z > 0.0 ? v.y : -v.y);
    }
    w = max(w, 1e-6);
    float compared = max(w * min(1.0, front / max(length(v), 1e-6)), at.w);
    vec4 d = pointShadow.depth[slot];
    // The depth at `compared` less kPointDepthBias texels' size there (d.y < 0: nearer is less).
    float z = d.x + d.y / compared + d.y * (kPointDepthBias * 2.0 / texels) / compared;
    float margin = (kPointRadius + 1.0) / texels;
    vec2 uv = clamp(vec2(xy.x, -xy.y) / w * 0.5 + 0.5, margin, 1.0 - margin);
    vec2 tile = vec2(face, float(slot));
    vec2 scale = vec2(1.0 / 6.0, 1.0 / 4.0);
    float lit = 0.0;
    for (int k = 0; k < kPointTaps; ++k) {
        vec2 o = shadow_disc(k, kPointTaps, turn) * (kPointRadius / texels);
        lit += textureLod(sampler2DShadow(point_shadow_map, point_shadow_smp), vec3((tile + uv + o) * scale, z), 0.0);
    }
    return lit / kPointTaps;
}
@end
