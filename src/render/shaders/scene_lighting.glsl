@block scene_lighting
vec3 ground_light(vec3 p, vec3 n, vec3 m, vec3 mp, float loss, float hidden, float strength, bool shadows, vec2 turn,
                  out vec3 spec) {
    vec3 ambient = ambient_colour.rgb * clamp(1.0 - loss, 0.0, 1.0);
    float facing = -dot(sun_dir.xyz, n);
    vec3 direct = facing > 0.0 ? min(vec3(1.0), m + facing * sun_colour.rgb) : m;
    vec3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * clamp(-dot(map_sun.xyz, n) * 6.0, 0.0, 1.0);
    vec3 lit = min(mix(sunlit, min(vec3(1.0), ambient + m), hidden), sunlit * shade);
    spec = vec3(0.0);
    int count = int(light_counts.x);
    for (int k = 0; k < count; ++k) {
        vec4 at = point_lights[2 * k], colour = point_lights[2 * k + 1];
        vec3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        vec3 to = d * inversesqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turned = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        float slot = dot(point_slots[k >> 2], vec4(equal(ivec4(0, 1, 2, 3), ivec4(k & 3))));
        float seen = 1.0;
        if (shadows && slot > 0.5) seen = point_seen(int(slot) - 1, p, n, kNoPointFront, turn);
        vec3 v = min(vec3(1.0), mp + turned * falloff * under * seen * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(vec3(1.0), max(lit, spec));
}
@end
