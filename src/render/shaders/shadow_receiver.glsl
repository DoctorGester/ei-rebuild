@block shadow_receiver
// The shadow map's receivers' part (included in the terrain and mesh shaders): how much of the sun the map
// hides at a pixel (0 .. 1). `s`: the pixel in the map's clip space, `dsx` / `dsy` its screen derivatives
// (taken before any discard). Soft edges as PCSS. See docs/notes/renderer.md, "The renderer's source files".
const int kSearchTaps = 8;
const int kFilterTaps = 16;
const int kFewTaps = 8;
const float kShadowCell = 16.0;   // kRangeCell
vec2 shadow_disc(int k, int n, vec2 turn) {
    float r = sqrt((k + 0.5) / n);
    float a = k * 2.39996323;   // the golden angle
    return mat2(turn.x, turn.y, -turn.y, turn.x) * (vec2(cos(a), sin(a)) * r);
}
// The discs' turn at a screen pixel (interleaved gradient noise): cosine and sine.
vec2 pixel_turn(vec2 pixel) {
    float a = 6.2831853 * fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    return vec2(cos(a), sin(a));
}
float shadow_filter(texture2D map, samplerShadow smp, vec2 uv, float z, vec2 slope, float radius, vec2 turn, int n) {
    float lit = 0.0;
    for (int j = 0; j < n; ++j) {
        vec2 o = shadow_disc(j, n, turn) * radius;
        lit += textureLod(sampler2DShadow(map, smp), vec3(uv + o, z + dot(slope, o)), 0.0);
    }
    return 1.0 - lit / n;
}
// A face's turn to the sun from its face normal (the derivatives, turned towards the camera `cam`): 1
// facing it, 0 turned away.
float shadow_facing(vec3 s, vec3 dsx, vec3 dsy, vec4 scale, vec4 cam) {
    vec3 n = cross(dsx * scale.xyz, dsy * scale.xyz);
    float len = length(n);
    if (len <= 1e-20) return 1.0;
    n /= len;
    if (dot(n, (cam.xyz - s) * scale.xyz) < 0.0) n = -n;
    return clamp(-n.z * 6.0, 0.0, 1.0);   // the sun's light travels along +z
}
float shadow_amount(texture2D map, samplerShadow smp, texture2D range, sampler range_smp, vec3 s, vec3 dsx, vec3 dsy,
                    vec4 on, vec4 scale, vec4 soft, vec2 pixel, float front) {
    if (on.x < 0.5) return 0.0;
    vec2 uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || s.z > 1.0) return 0.0;
    vec2 ux = vec2(dsx.x * 0.5, -dsx.y * 0.5), uy = vec2(dsy.x * 0.5, -dsy.y * 0.5);
    float det = ux.x * uy.y - ux.y * uy.x;
    vec2 slope = vec2(0.0);
    if (abs(det) > 1e-24) slope = vec2(uy.y * dsx.z - ux.y * dsy.z, ux.x * dsy.z - uy.x * dsx.z) / det;
    slope = clamp(slope, -scale.w, scale.w);
    float z = s.z - on.w;
    // A unit's mesh: compared as a plane facing the light at its object's nearest depth (`front`;
    // above 1: not a unit), so only what lies nearer the sun than all of the unit shadows it.
    if (front <= 1.0) {
        z = min(s.z, front) - on.w;
        slope = vec2(0.0);
    }
    vec4 around = textureLod(sampler2D(range, range_smp), uv, 0.0);
    float reach = (abs(slope.x) + abs(slope.y)) * soft.y;   // the receiver's plane across the search
    if (around.x >= z + reach) return 0.0;
    float widest = clamp((z + reach - around.x) * soft.x, soft.z, soft.y);
    vec2 turn = pixel_turn(pixel);
    if (around.y < z - reach) return 1.0;
    float radius = widest;
    float searched = clamp(widest * soft.w * 2.0 / kShadowCell - 1.0, 0.0, 1.0);   // 0 up to half a cell, 1 from a cell
    if (searched > 0.0) {
        float apart = 0.0, hidden = 0.0;
        if (around.z < z) {
            apart += z - around.z;
            hidden += 1.0;
        }
        for (int k = 0; k < kSearchTaps; ++k) {
            vec2 o = shadow_disc(k, kSearchTaps, turn) * widest;
            float zo = z + dot(slope, o);
            float d = textureLod(sampler2D(range, range_smp), uv + o, 0.0).z;
            if (d < zo) {
                apart += zo - d;
                hidden += 1.0;
            }
        }
        if (hidden == 0.0 && searched >= 1.0) return 0.0;
        float found = hidden > 0.0 ? clamp(apart / hidden * soft.x, soft.z, widest) : soft.z;
        radius = mix(widest, found, searched);
    }
    vec2 turn2 = mat2(turn.x, turn.y, -turn.y, turn.x) * vec2(0.70710678);
    float part;
    if (radius * soft.w < 4.0) part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFewTaps);
    else part = shadow_filter(map, smp, uv, z, slope, radius, turn2, kFilterTaps);
    return part;
}
@end
